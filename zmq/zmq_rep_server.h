#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "i_zmq_codec.h"
#include "zmq_config.h"

namespace zmq {
class socket_t;
}

// ZMQ REP 服务端：支持 tick 驱动模式和后台线程模式。
// 用法：
//   auto server = std::make_shared<ZmqRepServer>("ipc:///tmp/test.sock");
//   server->SetHandler([](const std::string& req) { return "reply"; });
//   server->Start();
//   // tick 模式：在主循环里调用 server->PollOnce(5);
//   // 或后台线程模式：server->StartBackground();
class ZmqRepServer {
public:
    using Handler = std::function<std::string(const std::string& payload)>;

    explicit ZmqRepServer(std::string endpoint,
                          std::shared_ptr<IZmqCodec> codec = nullptr);
    ~ZmqRepServer() noexcept;

    ZmqRepServer(const ZmqRepServer&) = delete;
    ZmqRepServer& operator=(const ZmqRepServer&) = delete;

    bool Start(const ZmqSocketOptions& opts = {});
    void Stop() noexcept;

    void SetHandler(Handler handler);

    // tick 模式：调用方在自己的循环里调用，timeout_ms=0 非阻塞。
    // 返回 true 表示处理了一条消息。
    bool PollOnce(int timeout_ms = 0);

    // 后台线程模式：Start() 后调用，自动在内部线程循环 PollOnce。
    void StartBackground();

private:
    void ApplyOptions(const ZmqSocketOptions& opts);
    bool RecvAndReply();
    void BackgroundLoop();

    std::string                    m_endpoint;
    std::shared_ptr<IZmqCodec>     m_codec;
    std::shared_ptr<zmq::socket_t> m_socket;  // shared_ptr 解决 PollOnce 竞争
    Handler                        m_handler;
    std::mutex                     m_mutex;
    std::atomic<bool>              m_running{false};
    std::thread                    m_thread;
};
