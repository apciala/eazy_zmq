#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <zmq.hpp>

#include "zmq_options.h"

namespace zmq_wrap {

class ReplierSocket {
public:
    using Handler = std::function<std::string(const std::string& payload)>;

    explicit ReplierSocket(std::string endpoint);
    ~ReplierSocket() noexcept;

    ReplierSocket(const ReplierSocket&)            = delete;
    ReplierSocket& operator=(const ReplierSocket&) = delete;

    bool Bind(const SocketConfig& cfg = {});
    void Stop() noexcept;
    void SetHandler(Handler handler);
    bool PollOnce(int timeout_ms = 0);
    // Returns false if already running or not yet bound.
    bool StartBackground();
    SocketStats GetStats() const;

private:
    void ApplyOptions(const SocketConfig& cfg);
    void BackgroundLoop();

    std::string                        m_endpoint;
    // shared_ptr (not optional): PollOnce() copies a local ref under lock,
    // then uses it outside the lock. This prevents dangling-pointer access
    // if Stop() concurrently calls m_sock.reset().
    std::shared_ptr<zmq::socket_t>     m_sock;
    Handler                            m_handler;
    mutable std::mutex                 m_mutex;
    std::atomic<bool>                  m_running{false};
    std::thread                        m_thread;
    SocketStats                        m_stats{};
};

}  // namespace zmq_wrap
