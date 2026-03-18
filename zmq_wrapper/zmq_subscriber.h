#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <zmq.hpp>

#include "zmq_options.h"

namespace zmq_wrap {

class SubscriberSocket {
public:
    using TopicHandler = std::function<void(const std::string& topic,
                                            const std::string& payload)>;

    explicit SubscriberSocket(std::string endpoint);
    ~SubscriberSocket() noexcept;

    SubscriberSocket(const SubscriberSocket&)            = delete;
    SubscriberSocket& operator=(const SubscriberSocket&) = delete;

    bool Connect(const SocketConfig& cfg = {});
    void Disconnect() noexcept;
    // Register a handler for a specific topic (or "" for all topics).
    // Can be called before or after Connect() — subscriptions registered before
    // Connect() are applied automatically when the socket is created.
    void Subscribe(const std::string& topic, TopicHandler handler);
    // Starts background thread to receive messages and dispatch to handlers.
    // Returns false if already running or not yet connected.
    // WARNING: Do NOT call Subscribe(), Disconnect(), or Stop() from within
    // a TopicHandler callback — it will cause deadlock. If you need dynamic
    // subscription, set a flag in the handler and call Subscribe() from another thread.
    bool StartBackground();
    void Stop() noexcept;
    SocketStats GetStats() const;

private:
    void RecvLoop();
    bool RecvMultipart(std::string& topic, std::string& payload);
    void ApplyOptions(const SocketConfig& cfg);

    std::string                         m_endpoint;
    // optional (not shared_ptr): RecvLoop accesses m_sock under lock throughout.
    // Disconnect() joins the thread before reset(), so no concurrent access.
    std::optional<zmq::socket_t>        m_sock;
    std::map<std::string, TopicHandler> m_handlers;
    mutable std::mutex                  m_mutex;
    std::atomic<bool>                   m_running{false};
    std::thread                         m_thread;
    SocketStats                         m_stats{};
};

}  // namespace zmq_wrap
