#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "i_zmq_codec.h"
#include "zmq_config.h"

namespace zmq {
class socket_t;
}

// ZMQ PUB 发布端：线程安全，无状态发布。
// 用法：
//   ZmqPublisher pub("ipc:///tmp/events.sock");
//   pub.Bind();
//   pub.Publish("sensor/temp", "{\"value\":36.5}");
class ZmqPublisher {
public:
    explicit ZmqPublisher(std::string endpoint,
                          std::shared_ptr<IZmqCodec> codec = nullptr);
    ~ZmqPublisher() noexcept;

    ZmqPublisher(const ZmqPublisher&) = delete;
    ZmqPublisher& operator=(const ZmqPublisher&) = delete;

    bool Bind(const ZmqSocketOptions& opts = {});
    void Unbind() noexcept;

    // 线程安全发布：topic 作为 multipart 第一帧，payload 为第二帧。
    // topic 为空时退化为单帧发送。
    bool Publish(const std::string& topic, const std::string& payload);

private:
    std::string                    m_endpoint;
    std::shared_ptr<IZmqCodec>     m_codec;
    std::unique_ptr<zmq::socket_t> m_socket;
    std::mutex                     m_mutex;
};
