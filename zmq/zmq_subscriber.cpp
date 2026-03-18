#include "zmq_subscriber.h"

#include <zmq.hpp>
#include <zmq_addon.hpp>

#include "all_include.h"
#include "zmq_context.h"
#include "zmq_codec_passthrough.h"

ZmqSubscriber::ZmqSubscriber(std::string endpoint, std::shared_ptr<IZmqCodec> codec)
    : m_endpoint(std::move(endpoint))
    , m_codec(codec ? std::move(codec) : std::make_shared<ZmqPassthroughCodec>())
{
}

ZmqSubscriber::~ZmqSubscriber() noexcept
{
    Stop();
}

bool ZmqSubscriber::Connect(const ZmqSocketOptions& opts)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_socket) {
        return true;
    }

    try {
        auto sock = std::make_shared<zmq::socket_t>(ZmqContext::Get(), zmq::socket_type::sub);
        sock->set(zmq::sockopt::rcvtimeo, opts.recv_timeout_ms);
        sock->set(zmq::sockopt::rcvhwm, opts.rcv_hwm);
        sock->set(zmq::sockopt::linger, opts.linger_ms);
        sock->connect(m_endpoint);

        // 应用已注册的订阅
        for (const auto& [topic, _] : m_handlers) {
            sock->set(zmq::sockopt::subscribe, topic);
        }

        m_socket = std::move(sock);
    } catch (const zmq::error_t& e) {
        log_error << "ZmqSubscriber connect failed: " << m_endpoint << ", err=" << e.what();
        return false;
    }

    return true;
}

void ZmqSubscriber::Disconnect() noexcept
{
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_socket.reset();
}

void ZmqSubscriber::Subscribe(const std::string& topic, TopicHandler handler)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handlers[topic] = std::move(handler);

    // 如果 socket 已连接，立即应用订阅
    if (m_socket) {
        try {
            m_socket->set(zmq::sockopt::subscribe, topic);
        } catch (const zmq::error_t& e) {
            log_error << "ZmqSubscriber subscribe failed: topic=" << topic << ", err=" << e.what();
        }
    }
}

void ZmqSubscriber::StartBackground()
{
    m_running = true;
    m_thread = std::thread(&ZmqSubscriber::RecvLoop, this);
}

void ZmqSubscriber::Stop() noexcept
{
    Disconnect();
}

void ZmqSubscriber::RecvLoop()
{
    while (m_running) {
        // 锁内取 socket 副本
        std::shared_ptr<zmq::socket_t> sock;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            sock = m_socket;
        }

        if (!sock) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::vector<zmq::message_t> parts;
        try {
            const auto result = zmq::recv_multipart(*sock, std::back_inserter(parts));
            if (!result.has_value() || parts.empty()) {
                continue;
            }
        } catch (const zmq::error_t& e) {
            if (e.num() == ETERM) {
                break;  // context 终止，正常退出
            }
            if (e.num() != EAGAIN) {
                log_error << "ZmqSubscriber recv error: " << e.what();
            }
            continue;
        }

        // multipart: [topic][payload] 或单帧 [payload]
        std::string topic;
        std::string raw;
        if (parts.size() >= 2) {
            topic = parts[0].to_string();
            raw   = parts[1].to_string();
        } else {
            raw = parts[0].to_string();
        }

        TransportMessage msg;
        if (!m_codec->Decode(raw, msg)) {
            log_error << "ZmqSubscriber decode failed";
            continue;
        }
        msg.topic = topic;

        // 分发：精确匹配 topic，或空 topic 通配
        TopicHandler handler;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_handlers.find(topic);
            if (it != m_handlers.end()) {
                handler = it->second;
            } else {
                auto wildcard = m_handlers.find("");
                if (wildcard != m_handlers.end()) {
                    handler = wildcard->second;
                }
            }
        }

        if (handler) {
            handler(msg.topic, msg.payload);
        }
    }
}
