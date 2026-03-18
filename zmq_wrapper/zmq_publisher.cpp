#include "zmq_publisher.h"

#include <cassert>

#include "logger.h"
#include "zmq_context.h"

namespace zmq_wrap {

PublisherSocket::PublisherSocket(std::string endpoint)
    : m_endpoint(std::move(endpoint))
{
}

PublisherSocket::~PublisherSocket() noexcept
{
    Unbind();
}

bool PublisherSocket::Bind(const SocketConfig& cfg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sock) {
        log_warn << "[ZMQ] PublisherSocket already bound, endpoint=" << m_endpoint;
        return true;
    }

    try {
        m_sock.emplace(ZmqGlobalCtx::get(), zmq::socket_type::pub);
        ApplyOptions(cfg);
        m_sock->bind(m_endpoint);
        log_info << "[ZMQ] PublisherSocket bound, endpoint=" << m_endpoint;
        return true;
    } catch (const zmq::error_t& e) {
        log_error << "[ZMQ] PublisherSocket bind failed"
                  << " endpoint=" << m_endpoint
                  << " error=" << e.what();
        m_sock.reset();
        return false;
    }
}

void PublisherSocket::Unbind() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sock) {
        m_sock.reset();
        log_info << "[ZMQ] PublisherSocket closed, endpoint=" << m_endpoint;
    }
}

bool PublisherSocket::Publish(const std::string& topic, const std::string& payload)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sock) {
        log_error << "[ZMQ] PublisherSocket::Publish: not bound, endpoint=" << m_endpoint;
        ++m_stats.send_fail;
        return false;
    }

    try {
        if (!topic.empty()) {
            m_sock->send(zmq::buffer(topic), zmq::send_flags::sndmore);
        }
        m_sock->send(zmq::buffer(payload), zmq::send_flags::none);
        ++m_stats.send_ok;
        return true;
    } catch (const zmq::error_t& e) {
        log_error << "[ZMQ] PublisherSocket send failed"
                  << " endpoint=" << m_endpoint
                  << " error=" << e.what();
        ++m_stats.send_fail;
        return false;
    }
}

bool PublisherSocket::Publish(const std::string& payload)
{
    return Publish("", payload);
}

SocketStats PublisherSocket::GetStats() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

void PublisherSocket::ApplyOptions(const SocketConfig& cfg)
{
    assert(m_sock);
    m_sock->set(zmq::sockopt::linger, cfg.linger_ms);
    m_sock->set(zmq::sockopt::sndhwm, cfg.send_hwm);
    if (cfg.send_timeout_ms >= 0) {
        m_sock->set(zmq::sockopt::sndtimeo, cfg.send_timeout_ms);
    }
}

}  // namespace zmq_wrap
