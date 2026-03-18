#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <zmq.hpp>

#include "zmq_options.h"

namespace zmq_wrap {

class RouterSocket {
public:
    using RecvHandler = std::function<void(const RoutedFrames& msg)>;

    explicit RouterSocket(std::string endpoint);
    ~RouterSocket() noexcept;

    RouterSocket(const RouterSocket&)            = delete;
    RouterSocket& operator=(const RouterSocket&) = delete;

    bool Bind(const SocketConfig& cfg = {});
    void Unbind() noexcept;
    std::optional<RoutedMessage> Recv(int timeout_ms = -1);
    // 接收任意帧数的消息，frames 包含 identity 之后的所有帧。
    // 适用于对端用 WithSocket 发送自定义多帧的场景。
    std::optional<RoutedFrames>  RecvAll(int timeout_ms = -1);
    bool SendTo(const std::string& peer_id, const std::string& payload);
    bool SendTo(const RoutedMessage& msg);
    // Starts background thread to receive messages and dispatch to handler.
    // Returns false if already running or not yet bound.
    // WARNING: Do NOT call Unbind() or Stop() from within the RecvHandler
    // callback — it will cause deadlock. If you need to stop, set a flag
    // in the handler and call Stop() from another thread.
    bool StartBackground(RecvHandler handler);
    void Stop() noexcept;
    SocketStats GetStats() const;

    // 高级接口：直接操作原生 socket，适用于自定义多帧等场景。
    // 回调在持锁期间执行，禁止在回调内再调用本对象的任何方法（死锁）。
    // 若 socket 未绑定则抛出 std::runtime_error。
    template <typename Func>
    auto WithSocket(Func&& func) -> decltype(func(std::declval<zmq::socket_t&>()))
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_sock) {
            throw std::runtime_error("RouterSocket not bound: " + m_endpoint);
        }
        return std::forward<Func>(func)(*m_sock);
    }

private:
    void ApplyOptions(const SocketConfig& cfg);
    void RecvLoop(RecvHandler handler);

    std::string                  m_endpoint;
    std::optional<zmq::socket_t> m_sock;
    mutable std::mutex           m_mutex;
    std::atomic<bool>            m_running{false};
    std::thread                  m_thread;
    SocketStats                  m_stats{};
};

}  // namespace zmq_wrap
