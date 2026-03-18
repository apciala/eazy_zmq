#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "i_zmq_codec.h"
#include "zmq_config.h"

namespace zmq {
class socket_t;
}

// ZMQ SUB 订阅端：后台线程接收，按 topic 分发回调。
// 用法：
//   ZmqSubscriber sub("ipc:///tmp/events.sock");
//   sub.Connect();
//   sub.Subscribe("sensor/temp", [](const std::string& topic, const std::string& payload) {
//       // 在后台线程中调用
//   });
//   sub.StartBackground();
//   // ...
//   sub.Stop();
class ZmqSubscriber {
public:
    using TopicHandler = std::function<void(const std::string& topic,
                                            const std::string& payload)>;

    explicit ZmqSubscriber(std::string endpoint,
                           std::shared_ptr<IZmqCodec> codec = nullptr);
    ~ZmqSubscriber() noexcept;

    ZmqSubscriber(const ZmqSubscriber&) = delete;
    ZmqSubscriber& operator=(const ZmqSubscriber&) = delete;

    bool Connect(const ZmqSocketOptions& opts = {});
    void Disconnect() noexcept;

    // 订阅 topic，收到消息时在后台线程调用 handler。
    // 可在 StartBackground() 前或后调用。
    // topic 为空字符串表示订阅所有消息。
    void Subscribe(const std::string& topic, TopicHandler handler);

    void StartBackground();
    void Stop() noexcept;

private:
    void RecvLoop();

    std::string                              m_endpoint;
    std::shared_ptr<IZmqCodec>               m_codec;
    std::shared_ptr<zmq::socket_t>           m_socket;  // shared_ptr 解决 Stop() 竞争
    std::map<std::string, TopicHandler>      m_handlers;
    std::mutex                               m_mutex;
    std::atomic<bool>                        m_running{false};
    std::thread                              m_thread;
};
