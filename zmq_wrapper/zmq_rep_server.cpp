#include "zmq_rep_server.h"

#include <cassert>

#include "logger.h"
#include "zmq_context.h"
#include "zmq_options.h"

using zmq_wrap::detail::kPollTimeoutMs;

namespace zmq_wrap {

ReplierSocket::ReplierSocket(std::string endpoint)
    : m_endpoint(std::move(endpoint))
{
}

ReplierSocket::~ReplierSocket() noexcept
{
    Stop();
}

bool ReplierSocket::Bind(const SocketConfig& cfg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sock) {
        log_warn << "[ZMQ] ReplierSocket already bound, endpoint=" << m_endpoint;
        return true;
    }

    try {
        m_sock = std::make_shared<zmq::socket_t>(ZmqGlobalCtx::get(), zmq::socket_type::rep);
        ApplyOptions(cfg);
        m_sock->bind(m_endpoint);
        log_info << "[ZMQ] ReplierSocket bound, endpoint=" << m_endpoint;
        return true;
    } catch (const zmq::error_t& e) {
        log_error << "[ZMQ] ReplierSocket bind failed"
                  << " endpoint=" << m_endpoint << " error=" << e.what();
        m_sock.reset();
        return false;
    }
}

void ReplierSocket::Stop() noexcept
{
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_sock.reset();
    log_info << "[ZMQ] ReplierSocket closed, endpoint=" << m_endpoint;
}

void ReplierSocket::SetHandler(Handler handler)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handler = std::move(handler);
}

bool ReplierSocket::PollOnce(int timeout_ms)
{
    // Copy both sock and handler under the lock.
    // shared_ptr keeps the socket alive even if Stop() calls m_sock.reset()
    // concurrently — the socket object is only destroyed after this local ref
    // goes out of scope, so there is no dangling-pointer access below.
    std::shared_ptr<zmq::socket_t> sock;
    Handler handler;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_sock || !m_handler) return false;
        sock    = m_sock;
        handler = m_handler;
    }

    try {
        zmq::pollitem_t items[] = {{sock->handle(), 0, ZMQ_POLLIN, 0}};
        int rc = zmq::poll(items, 1, std::chrono::milliseconds(timeout_ms));
        if (rc <= 0 || !(items[0].revents & ZMQ_POLLIN)) return false;

        zmq::message_t req_msg;
        (void)sock->recv(req_msg);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_stats.recv_ok;
        }

        std::string payload(static_cast<char*>(req_msg.data()), req_msg.size());
        std::string reply = handler(payload);

        sock->send(zmq::buffer(reply), zmq::send_flags::none);
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_stats.send_ok;
        }
        return true;
    } catch (const zmq::error_t& e) {
        if (e.num() == ETERM) {
            log_info << "[ZMQ] ReplierSocket context terminated";
            m_running = false;
        } else {
            log_error << "[ZMQ] ReplierSocket error endpoint=" << m_endpoint
                      << " error=" << e.what();
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_stats.recv_fail;
        }
        return false;
    }
}

bool ReplierSocket::StartBackground()
{
    if (m_thread.joinable()) {
        log_warn << "[ZMQ] ReplierSocket background already running, endpoint=" << m_endpoint;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_sock) {
            log_error << "[ZMQ] ReplierSocket::StartBackground: not bound, endpoint=" << m_endpoint;
            return false;
        }
    }
    m_running = true;
    m_thread = std::thread(&ReplierSocket::BackgroundLoop, this);
    log_info << "[ZMQ] ReplierSocket background thread started, endpoint=" << m_endpoint;
    return true;
}

SocketStats ReplierSocket::GetStats() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

void ReplierSocket::ApplyOptions(const SocketConfig& cfg)
{
    assert(m_sock);
    m_sock->set(zmq::sockopt::linger,  cfg.linger_ms);
    m_sock->set(zmq::sockopt::sndhwm,  cfg.send_hwm);
    m_sock->set(zmq::sockopt::rcvhwm,  cfg.recv_hwm);
    if (cfg.send_timeout_ms >= 0) {
        m_sock->set(zmq::sockopt::sndtimeo, cfg.send_timeout_ms);
    }
    if (cfg.recv_timeout_ms >= 0) {
        m_sock->set(zmq::sockopt::rcvtimeo, cfg.recv_timeout_ms);
    }
}

void ReplierSocket::BackgroundLoop()
{
    while (m_running) {
        PollOnce(kPollTimeoutMs);
    }
}

}  // namespace zmq_wrap
