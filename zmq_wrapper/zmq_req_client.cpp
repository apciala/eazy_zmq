#include "zmq_req_client.h"

#include <cassert>

#include "logger.h"
#include "zmq_context.h"

namespace zmq_wrap {

RequesterSocket::RequesterSocket(std::string endpoint)
    : m_endpoint(std::move(endpoint))
{
}

RequesterSocket::~RequesterSocket() noexcept
{
    Disconnect();
}

bool RequesterSocket::Connect(const SocketConfig& cfg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sock) {
        log_warn << "[ZMQ] RequesterSocket already connected, endpoint=" << m_endpoint;
        return true;
    }

    try {
        m_sock.emplace(ZmqGlobalCtx::get(), zmq::socket_type::req);
        ApplyOptions(cfg);
        m_sock->connect(m_endpoint);
        log_info << "[ZMQ] RequesterSocket connected, endpoint=" << m_endpoint;
        return true;
    } catch (const zmq::error_t& e) {
        log_error << "[ZMQ] RequesterSocket connect failed"
                  << " endpoint=" << m_endpoint << " error=" << e.what();
        m_sock.reset();
        return false;
    }
}

void RequesterSocket::Disconnect() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sock) {
        m_sock.reset();
        log_info << "[ZMQ] RequesterSocket closed, endpoint=" << m_endpoint;
    }
}

std::optional<std::string> RequesterSocket::Request(const std::string& payload,
                                                      int timeout_ms)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sock) {
        log_error << "[ZMQ] RequesterSocket::Request: not connected, endpoint=" << m_endpoint;
        ++m_stats.send_fail;
        return std::nullopt;
    }

    try {
        m_sock->send(zmq::buffer(payload), zmq::send_flags::none);
        ++m_stats.send_ok;

        // Use poll to control timeout without modifying socket options
        zmq::pollitem_t items[] = {{m_sock->handle(), 0, ZMQ_POLLIN, 0}};
        int rc = zmq::poll(items, 1, std::chrono::milliseconds(timeout_ms));
        if (rc <= 0 || !(items[0].revents & ZMQ_POLLIN)) {
            log_warn << "[ZMQ] RequesterSocket recv timeout"
                     << " endpoint=" << m_endpoint
                     << " timeout_ms=" << timeout_ms;
            ++m_stats.eagain;
            return std::nullopt;
        }

        zmq::message_t msg;
        (void)m_sock->recv(msg, zmq::recv_flags::none);  // Blocking recv after poll confirmed data
        ++m_stats.recv_ok;
        return std::string(static_cast<char*>(msg.data()), msg.size());
    } catch (const zmq::error_t& e) {
        if (e.num() == ETERM) {
            log_info << "[ZMQ] RequesterSocket context terminated";
        } else {
            log_error << "[ZMQ] RequesterSocket request failed"
                      << " endpoint=" << m_endpoint << " error=" << e.what();
            ++m_stats.recv_fail;
        }
        return std::nullopt;
    }
}

SocketStats RequesterSocket::GetStats() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

void RequesterSocket::ApplyOptions(const SocketConfig& cfg)
{
    assert(m_sock);
    m_sock->set(zmq::sockopt::linger, cfg.linger_ms);
    if (cfg.send_timeout_ms >= 0) {
        m_sock->set(zmq::sockopt::sndtimeo, cfg.send_timeout_ms);
    }
    // recv timeout is controlled by zmq::poll() in Request(), not sockopt
}

}  // namespace zmq_wrap
