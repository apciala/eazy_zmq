#include "zmq_dealer.h"

#include <cassert>

#include "logger.h"
#include "zmq_context.h"
#include "zmq_options.h"

using zmq_wrap::detail::kPollTimeoutMs;

namespace zmq_wrap {

DealerSocket::DealerSocket(std::string endpoint, const SocketConfig& cfg)
    : m_endpoint(std::move(endpoint))
    , m_cfg(cfg)
{
}

DealerSocket::~DealerSocket() noexcept
{
    Stop();
}

bool DealerSocket::Connect()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sock) {
        log_warn << "[ZMQ] DealerSocket already connected, endpoint=" << m_endpoint;
        return true;
    }

    try {
        m_sock.emplace(ZmqGlobalCtx::get(), zmq::socket_type::dealer);
        ApplyOptions();
        m_sock->connect(m_endpoint);
        log_info << "[ZMQ] DealerSocket connected"
                 << " endpoint=" << m_endpoint
                 << " identity=" << (m_cfg.identity.empty() ? "(auto)" : m_cfg.identity);
        return true;
    } catch (const zmq::error_t& e) {
        log_error << "[ZMQ] DealerSocket connect failed"
                  << " endpoint=" << m_endpoint << " error=" << e.what();
        m_sock.reset();
        return false;
    }
}

void DealerSocket::Disconnect() noexcept
{
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sock) {
        m_sock.reset();
        log_info << "[ZMQ] DealerSocket closed, endpoint=" << m_endpoint;
    }
}

bool DealerSocket::Send(const std::string& payload)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sock) {
        log_error << "[ZMQ] DealerSocket::Send: not connected, endpoint=" << m_endpoint;
        ++m_stats.send_fail;
        return false;
    }

    try {
        // DEALER sends single frame; ZMQ prepends identity automatically
        m_sock->send(zmq::buffer(payload), zmq::send_flags::none);
        log_debug << "[ZMQ] DealerSocket send ok"
                  << " endpoint=" << m_endpoint
                  << " payload_size=" << payload.size();
        ++m_stats.send_ok;
        return true;
    } catch (const zmq::error_t& e) {
        log_error << "[ZMQ] DealerSocket send failed"
                  << " endpoint=" << m_endpoint << " error=" << e.what();
        ++m_stats.send_fail;
        return false;
    }
}

std::optional<std::string> DealerSocket::Recv(int timeout_ms)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sock) {
        log_error << "[ZMQ] DealerSocket::Recv: not connected, endpoint=" << m_endpoint;
        return std::nullopt;
    }

    try {
        m_sock->set(zmq::sockopt::rcvtimeo, timeout_ms);

        zmq::message_t msg;
        auto result = m_sock->recv(msg);
        if (!result) {
            ++m_stats.eagain;
            return std::nullopt;
        }

        std::string payload(static_cast<char*>(msg.data()), msg.size());
        log_debug << "[ZMQ] DealerSocket recv ok"
                  << " endpoint=" << m_endpoint
                  << " payload_size=" << payload.size();
        ++m_stats.recv_ok;
        return payload;
    } catch (const zmq::error_t& e) {
        if (e.num() == ETERM) {
            log_info << "[ZMQ] DealerSocket context terminated";
        } else {
            log_error << "[ZMQ] DealerSocket recv failed"
                      << " endpoint=" << m_endpoint << " error=" << e.what();
            ++m_stats.recv_fail;
        }
        return std::nullopt;
    }
}

bool DealerSocket::StartBackground(RecvHandler handler)
{
    if (m_thread.joinable()) {
        log_warn << "[ZMQ] DealerSocket background already running, endpoint=" << m_endpoint;
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_sock) {
            log_error << "[ZMQ] DealerSocket::StartBackground: not connected, endpoint=" << m_endpoint;
            return false;
        }
    }
    m_running = true;
    m_thread  = std::thread(&DealerSocket::RecvLoop, this, std::move(handler));
    log_info << "[ZMQ] DealerSocket background thread started, endpoint=" << m_endpoint;
    return true;
}

void DealerSocket::Stop() noexcept
{
    Disconnect();
}

SocketStats DealerSocket::GetStats() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

void DealerSocket::ApplyOptions()
{
    assert(m_sock);
    m_sock->set(zmq::sockopt::linger, m_cfg.linger_ms);
    m_sock->set(zmq::sockopt::sndhwm, m_cfg.send_hwm);
    m_sock->set(zmq::sockopt::rcvhwm, m_cfg.recv_hwm);
    if (!m_cfg.identity.empty()) {
        m_sock->set(zmq::sockopt::routing_id, m_cfg.identity);
    }
    if (m_cfg.send_timeout_ms >= 0) {
        m_sock->set(zmq::sockopt::sndtimeo, m_cfg.send_timeout_ms);
    }
    // Set short recv timeout so background thread can check m_running
    m_sock->set(zmq::sockopt::rcvtimeo, kPollTimeoutMs);
}

void DealerSocket::RecvLoop(RecvHandler handler)
{
    while (m_running) {
        // Grab a raw pointer under lock, then recv outside the lock.
        // Safe because Disconnect() joins this thread before resetting m_sock.
        zmq::socket_t* raw_sock = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_sock) break;
            raw_sock = &(*m_sock);
        }

        zmq::message_t msg;
        bool got_msg = false;
        try {
            auto result = raw_sock->recv(msg);  // blocks up to rcvtimeo=100ms
            if (result) {
                got_msg = true;
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_stats.recv_ok;
            } else {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_stats.eagain;
            }
        } catch (const zmq::error_t& e) {
            if (e.num() == ETERM) {
                log_info << "[ZMQ] DealerSocket background: context terminated";
                m_running = false;
                break;
            }
            log_error << "[ZMQ] DealerSocket background recv failed"
                      << " endpoint=" << m_endpoint << " error=" << e.what();
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_stats.recv_fail;
        }

        if (got_msg) {
            handler(std::string(static_cast<char*>(msg.data()), msg.size()));
        }
    }
}

}  // namespace zmq_wrap
