#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "i_zmq_codec.h"
#include "zmq_config.h"

namespace zmq {
class socket_t;
}

// ZMQ REQ 客户端：同步调用，发送请求并等待回复，支持超时。
// 线程安全：内部 mutex 保护，可多线程调用 Call()。
// 用法：
//   ZmqReqClient client("ipc:///tmp/test.sock");
//   client.Connect();
//   auto reply = client.Call("{\"cmd\":\"ping\"}", 3000);
//   if (reply) { /* 处理 *reply */ }
class ZmqReqClient {
public:
    explicit ZmqReqClient(std::string endpoint,
                          std::shared_ptr<IZmqCodec> codec = nullptr);
    ~ZmqReqClient() noexcept;

    ZmqReqClient(const ZmqReqClient&) = delete;
    ZmqReqClient& operator=(const ZmqReqClient&) = delete;

    bool Connect(const ZmqSocketOptions& opts = {});
    void Disconnect() noexcept;

    // 同步调用：发送 payload，等待回复。
    // 超时或失败返回 std::nullopt。
    std::optional<std::string> Call(const std::string& payload, int timeout_ms = 3000);

private:
    std::string                    m_endpoint;
    std::shared_ptr<IZmqCodec>     m_codec;
    std::unique_ptr<zmq::socket_t> m_socket;
    std::mutex                     m_mutex;
};
