#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <zmq.hpp>

#include "zmq_options.h"

namespace zmq_wrap {

class RequesterSocket {
public:
    explicit RequesterSocket(std::string endpoint);
    ~RequesterSocket() noexcept;

    RequesterSocket(const RequesterSocket&)            = delete;
    RequesterSocket& operator=(const RequesterSocket&) = delete;

    bool Connect(const SocketConfig& cfg = {});
    void Disconnect() noexcept;
    std::optional<std::string> Request(const std::string& payload, int timeout_ms = 3000);
    SocketStats GetStats() const;

private:
    void ApplyOptions(const SocketConfig& cfg);

    std::string                  m_endpoint;
    std::optional<zmq::socket_t> m_sock;
    mutable std::mutex           m_mutex;
    SocketStats                  m_stats{};
};

}  // namespace zmq_wrap
