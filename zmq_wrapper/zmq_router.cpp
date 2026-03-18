#include "zmq_router.h"

#include <cassert>

#include "logger.h"
#include "zmq_context.h"

namespace zmq_wrap {

// ── DEALER/ROUTER 帧协议说明 ──────────────────────────────────────────
//
// DEALER → ROUTER（DEALER 发单帧，ZMQ 自动在前面插入 identity）：
//   ROUTER 接收到：[identity][payload]  ← 2帧，无 delimiter
//
// ROUTER → DEALER（ROUTER 发2帧，ZMQ 自动剥离 identity 再给 DEALER）：
//   ROUTER 发送：  [identity][payload]  ← 2帧，无 delimiter
//   DEALER 接收到：[payload]            ← 1帧
//
// delimiter（空帧）只存在于 REQ/REP 协议信封中，DEALER/ROUTER 直连不需要。
// ─────────────────────────────────────────────────────────────────────

RouterSocket::RouterSocket(std::string endpoint)
    : m_endpoint(std::move(endpoint))
{
}

RouterSocket::~RouterSocket() noexcept
{
    Stop();
    Unbind();
}

bool RouterSocket::Bind(const SocketConfig& cfg)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sock) {
        log_warn << "[ZMQ] RouterSocket already bound, endpoint=" << m_endpoint;
        return true;
    }

    try {
        m_sock.emplace(ZmqGlobalCtx::get(), zmq::socket_type::router);
        ApplyOptions(cfg);
        m_sock->bind(m_endpoint);
        log_info << "[ZMQ] RouterSocket bound, endpoint=" << m_endpoint;
        return true;
    } catch (const zmq::error_t& e) {
        log_error << "[ZMQ] RouterSocket bind failed"
                  << " endpoint=" << m_endpoint << " error=" << e.what();
        m_sock.reset();
        return false;
    }
}

void RouterSocket::Unbind() noexcept
{
    Stop();
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_sock) {
        m_sock.reset();
        log_info << "[ZMQ] RouterSocket closed, endpoint=" << m_endpoint;
    }
}

std::optional<RoutedMessage> RouterSocket::Recv(int timeout_ms)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sock) {
        log_error << "[ZMQ] RouterSocket::Recv: not bound, endpoint=" << m_endpoint;
        return std::nullopt;
    }

    try {
        m_sock->set(zmq::sockopt::rcvtimeo, timeout_ms);

        // 帧1：identity
        zmq::message_t id_msg;
        auto r1 = m_sock->recv(id_msg);
        if (!r1) {
            ++m_stats.eagain;
            return std::nullopt;
        }
        std::string peer_id(static_cast<char*>(id_msg.data()), id_msg.size());

        // 帧2：payload（DEALER/ROUTER 直连无 delimiter）
        zmq::message_t payload_msg;
        (void)m_sock->recv(payload_msg);
        std::string payload(static_cast<char*>(payload_msg.data()), payload_msg.size());

        log_debug << "[ZMQ] RouterSocket recv ok"
                  << " peer_id=" << peer_id
                  << " payload_size=" << payload.size();
        ++m_stats.recv_ok;
        return RoutedMessage{std::move(peer_id), std::move(payload)};
    } catch (const zmq::error_t& e) {
        if (e.num() == ETERM) {
            log_info << "[ZMQ] RouterSocket context terminated";
        } else {
            log_error << "[ZMQ] RouterSocket recv failed"
                      << " endpoint=" << m_endpoint << " error=" << e.what();
            ++m_stats.recv_fail;
        }
        return std::nullopt;
    }
}

std::optional<RoutedFrames> RouterSocket::RecvAll(int timeout_ms)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sock) {
        log_error << "[ZMQ] RouterSocket::RecvAll: not bound, endpoint=" << m_endpoint;
        return std::nullopt;
    }

    try {
        m_sock->set(zmq::sockopt::rcvtimeo, timeout_ms);

        // 帧1：identity
        zmq::message_t id_msg;
        if (!m_sock->recv(id_msg)) {
            ++m_stats.eagain;
            return std::nullopt;
        }
        std::string peer_id(static_cast<char*>(id_msg.data()), id_msg.size());

        // 剩余帧：用 rcvmore 判断是否还有帧，直到最后一帧
        RoutedFrames result;
        result.peer_id = std::move(peer_id);
        while (true) {
            zmq::message_t frame;
            (void)m_sock->recv(frame, zmq::recv_flags::none);
            result.frames.emplace_back(static_cast<char*>(frame.data()), frame.size());
            if (!frame.more()) break;
        }

        log_debug << "[ZMQ] RouterSocket RecvAll ok"
                  << " peer_id=" << result.peer_id
                  << " frames=" << result.frames.size();
        ++m_stats.recv_ok;
        return result;
    } catch (const zmq::error_t& e) {
        if (e.num() == ETERM) {
            log_info << "[ZMQ] RouterSocket context terminated";
        } else {
            log_error << "[ZMQ] RouterSocket RecvAll failed"
                      << " endpoint=" << m_endpoint << " error=" << e.what();
            ++m_stats.recv_fail;
        }
        return std::nullopt;
    }
}

bool RouterSocket::SendTo(const std::string& peer_id, const std::string& payload)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sock) {
        log_error << "[ZMQ] RouterSocket::SendTo: not bound, endpoint=" << m_endpoint;
        ++m_stats.send_fail;
        return false;
    }

    try {
        // 帧1：identity；帧2：payload
        m_sock->send(zmq::buffer(peer_id), zmq::send_flags::sndmore);
        m_sock->send(zmq::buffer(payload), zmq::send_flags::none);
        log_debug << "[ZMQ] RouterSocket send ok peer_id=" << peer_id;
        ++m_stats.send_ok;
        return true;
    } catch (const zmq::error_t& e) {
        log_error << "[ZMQ] RouterSocket send failed"
                  << " endpoint=" << m_endpoint << " error=" << e.what();
        ++m_stats.send_fail;
        return false;
    }
}

bool RouterSocket::SendTo(const RoutedMessage& msg)
{
    return SendTo(msg.peer_id, msg.payload);
}

SocketStats RouterSocket::GetStats() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

bool RouterSocket::StartBackground(RecvHandler handler)
{
    if (m_thread.joinable()) {
        log_warn << "[ZMQ] RouterSocket background thread already running";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_sock) {
            log_error << "[ZMQ] RouterSocket::StartBackground: not bound";
            return false;
        }
    }

    m_running = true;
    m_thread = std::thread(&RouterSocket::RecvLoop, this, std::move(handler));
    log_info << "[ZMQ] RouterSocket background thread started, endpoint=" << m_endpoint;
    return true;
}

void RouterSocket::Stop() noexcept
{
    if (!m_running) return;

    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
        log_info << "[ZMQ] RouterSocket background thread stopped, endpoint=" << m_endpoint;
    }
}

void RouterSocket::RecvLoop(RecvHandler handler)
{
    while (m_running) {
        zmq::socket_t* raw_sock = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_sock) break;
            raw_sock = &(*m_sock);
        }

        // 使用 poll 替代直接 recv，设置超时以便响应停止信号
        zmq::pollitem_t items[] = {{raw_sock->handle(), 0, ZMQ_POLLIN, 0}};
        int rc = zmq::poll(items, 1, std::chrono::milliseconds(100));
        if (rc <= 0 || !(items[0].revents & ZMQ_POLLIN)) {
            continue;  // 超时或无数据，继续检查 m_running
        }

        // 帧1：identity
        zmq::message_t id_msg;
        try {
            auto result = raw_sock->recv(id_msg, zmq::recv_flags::none);
            if (!result) continue;
        } catch (const zmq::error_t& e) {
            if (e.num() == ETERM) {
                log_info << "[ZMQ] RouterSocket context terminated";
                break;
            }
            std::lock_guard<std::mutex> lock(m_mutex);
            log_error << "[ZMQ] RouterSocket recv failed: " << e.what();
            ++m_stats.recv_fail;
            continue;
        }

        // 剩余帧：用 rcvmore 判断
        RoutedFrames msg;
        msg.peer_id = std::string(static_cast<char*>(id_msg.data()), id_msg.size());

        while (true) {
            zmq::message_t frame;
            try {
                (void)raw_sock->recv(frame, zmq::recv_flags::none);
                msg.frames.emplace_back(static_cast<char*>(frame.data()), frame.size());
                if (!frame.more()) break;
            } catch (const zmq::error_t& e) {
                std::lock_guard<std::mutex> lock(m_mutex);
                log_error << "[ZMQ] RouterSocket recv frame failed: " << e.what();
                ++m_stats.recv_fail;
                break;
            }
        }

        if (!msg.frames.empty()) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_stats.recv_ok;
            }
            handler(msg);
        }
    }
}

void RouterSocket::ApplyOptions(const SocketConfig& cfg)
{
    assert(m_sock);
    m_sock->set(zmq::sockopt::linger, cfg.linger_ms);
    m_sock->set(zmq::sockopt::sndhwm, cfg.send_hwm);
    m_sock->set(zmq::sockopt::rcvhwm, cfg.recv_hwm);
    if (!cfg.identity.empty()) {
        m_sock->set(zmq::sockopt::routing_id, cfg.identity);
    }
}

}  // namespace zmq_wrap
