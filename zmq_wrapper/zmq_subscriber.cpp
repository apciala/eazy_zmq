#include "zmq_subscriber.h"

#include <cassert>

#include "logger.h"
#include "zmq_context.h"
#include "zmq_options.h"

using zmq_wrap::detail::kPollTimeoutMs;

namespace zmq_wrap {

SubscriberSocket::SubscriberSocket(std::string endpoint)
    : m_endpoint(std::move(endpoint))
{
}

SubscriberSocket::~SubscriberSocket() noexcept
{
    Stop();
}

bool SubscriberSocket::Connect(const SocketConfig& cfg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sock) {
        log_warn << "[ZMQ] SubscriberSocket already connected, endpoint=" << m_endpoint;
        return true;
    }

    try {
        m_sock.emplace(ZmqGlobalCtx::get(), zmq::socket_type::sub);
        ApplyOptions(cfg);
        m_sock->connect(m_endpoint);

        // Apply topic filters registered before Connect()
        for (const auto& [topic, _] : m_handlers) {
            m_sock->set(zmq::sockopt::subscribe, topic);
        }

        log_info << "[ZMQ] SubscriberSocket connected, endpoint=" << m_endpoint;
        return true;
    } catch (const zmq::error_t& e) {
        log_error << "[ZMQ] SubscriberSocket connect failed"
                  << " endpoint=" << m_endpoint << " error=" << e.what();
        m_sock.reset();
        return false;
    }
}

void SubscriberSocket::Disconnect() noexcept
{
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_sock.reset();
    log_info << "[ZMQ] SubscriberSocket closed, endpoint=" << m_endpoint;
}

void SubscriberSocket::Subscribe(const std::string& topic, TopicHandler handler)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers[topic] = std::move(handler);

    if (m_sock) {
        m_sock->set(zmq::sockopt::subscribe, topic);
        log_debug << "[ZMQ] SubscriberSocket subscribed topic='" << topic
                  << "' endpoint=" << m_endpoint;
    }
}

bool SubscriberSocket::StartBackground()
{
    if (m_thread.joinable()) {
        log_warn << "[ZMQ] SubscriberSocket background already running, endpoint=" << m_endpoint;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_sock) {
            log_error << "[ZMQ] SubscriberSocket::StartBackground: not connected, endpoint=" << m_endpoint;
            return false;
        }
    }
    m_running = true;
    m_thread  = std::thread(&SubscriberSocket::RecvLoop, this);
    log_info << "[ZMQ] SubscriberSocket background thread started, endpoint=" << m_endpoint;
    return true;
}

void SubscriberSocket::Stop() noexcept
{
    Disconnect();
}

SocketStats SubscriberSocket::GetStats() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

// ── internal ──────────────────────────────────────────────

void SubscriberSocket::RecvLoop()
{
    while (m_running) {
        std::string topic, payload;
        if (!RecvMultipart(topic, payload)) {
            continue;
        }

        TopicHandler handler;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_handlers.find(topic);
            if (it == m_handlers.end()) {
                it = m_handlers.find("");  // empty topic = subscribe-all
            }
            if (it != m_handlers.end()) {
                handler = it->second;
            }
            ++m_stats.recv_ok;
        }

        if (handler) {
            handler(topic, payload);
        } else {
            log_debug << "[ZMQ] SubscriberSocket no handler for topic='" << topic
                      << "' endpoint=" << m_endpoint;
        }
    }
}

bool SubscriberSocket::RecvMultipart(std::string& topic, std::string& payload)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_sock) return false;

    try {
        // Wire format mirrors PublisherSocket::Publish():
        //   two frames  → frame1=topic, frame2=payload
        //   single frame → topic="", payload=frame content (published with empty topic)
        zmq::message_t frame1;
        auto r1 = m_sock->recv(frame1);
        if (!r1) {
            ++m_stats.eagain;
            return false;
        }

        topic.assign(static_cast<char*>(frame1.data()), frame1.size());

        bool more = m_sock->get(zmq::sockopt::rcvmore);
        if (!more) {
            // Single-frame message: topic is also the payload
            payload = topic;
            topic.clear();
            return true;
        }

        zmq::message_t frame2;
        (void)m_sock->recv(frame2);
        payload.assign(static_cast<char*>(frame2.data()), frame2.size());
        return true;
    } catch (const zmq::error_t& e) {
        if (e.num() == ETERM) {
            log_info << "[ZMQ] SubscriberSocket context terminated, exiting loop";
            m_running = false;
        } else {
            log_error << "[ZMQ] SubscriberSocket recv failed"
                      << " endpoint=" << m_endpoint << " error=" << e.what();
            ++m_stats.recv_fail;
        }
        return false;
    }
}

void SubscriberSocket::ApplyOptions(const SocketConfig& cfg)
{
    assert(m_sock);
    m_sock->set(zmq::sockopt::linger,  cfg.linger_ms);
    m_sock->set(zmq::sockopt::rcvhwm,  cfg.recv_hwm);
    // Short recv timeout so background thread can check m_running
    m_sock->set(zmq::sockopt::rcvtimeo, kPollTimeoutMs);
}

}  // namespace zmq_wrap
