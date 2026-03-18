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

class DealerSocket {
public:
    using RecvHandler = std::function<void(const std::string& payload)>;

    explicit DealerSocket(std::string endpoint, const SocketConfig& cfg = {});
    ~DealerSocket() noexcept;

    DealerSocket(const DealerSocket&)            = delete;
    DealerSocket& operator=(const DealerSocket&) = delete;

    bool Connect();
    void Disconnect() noexcept;
    bool Send(const std::string& payload);
    std::optional<std::string> Recv(int timeout_ms = -1);
    // Starts background thread to receive messages and dispatch to handler.
    // Returns false if already running or not yet connected.
    // WARNING: Do NOT call Disconnect() or Stop() from within the RecvHandler
    // callback — it will cause deadlock (thread joining itself).
    bool StartBackground(RecvHandler handler);
    void Stop() noexcept;
    SocketStats GetStats() const;

private:
    void ApplyOptions();
    void RecvLoop(RecvHandler handler);

    std::string                  m_endpoint;
    SocketConfig                 m_cfg;
    // optional (not shared_ptr): RecvLoop accesses m_sock under lock throughout.
    // Disconnect() joins the thread before reset(), so no concurrent access.
    std::optional<zmq::socket_t> m_sock;
    mutable std::mutex           m_mutex;
    std::atomic<bool>            m_running{false};
    std::thread                  m_thread;
    SocketStats                  m_stats{};
};

}  // namespace zmq_wrap
