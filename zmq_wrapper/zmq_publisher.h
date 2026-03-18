#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <zmq.hpp>

#include "zmq_options.h"

namespace zmq_wrap {

class PublisherSocket {
public:
    explicit PublisherSocket(std::string endpoint);
    ~PublisherSocket() noexcept;

    PublisherSocket(const PublisherSocket&)            = delete;
    PublisherSocket& operator=(const PublisherSocket&) = delete;

    bool Bind(const SocketConfig& cfg = {});
    void Unbind() noexcept;
    // Wire format: non-empty topic → two frames [topic][payload].
    //              empty topic    → single frame [payload] (no topic frame sent).
    // Subscribers must use the matching SubscriberSocket to decode correctly.
    bool Publish(const std::string& topic, const std::string& payload);
    bool Publish(const std::string& payload);  // equivalent to Publish("", payload)
    SocketStats GetStats() const;

private:
    void ApplyOptions(const SocketConfig& cfg);

    std::string                  m_endpoint;
    std::optional<zmq::socket_t> m_sock;
    mutable std::mutex           m_mutex;
    SocketStats                  m_stats{};
};

}  // namespace zmq_wrap
