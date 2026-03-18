#include "zmq_publisher.h"

#include <zmq.hpp>

#include "all_include.h"
#include "zmq_context.h"
#include "zmq_codec_passthrough.h"

ZmqPublisher::ZmqPublisher(std::string endpoint, std::shared_ptr<IZmqCodec> codec)
    : m_endpoint(std::move(endpoint))
    , m_codec(codec ? std::move(codec) : std::make_shared<ZmqPassthroughCodec>())
{
}

ZmqPublisher::~ZmqPublisher() noexcept
{
    Unbind();
}

bool ZmqPublisher::Bind(const ZmqSocketOptions& opts)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_socket) {
        return true;
    }

    try {
        m_socket = std::make_unique<zmq::socket_t>(ZmqContext::Get(), zmq::socket_type::pub);
        m_socket->set(zmq::sockopt::sndtimeo, opts.send_timeout_ms);
        m_socket->set(zmq::sockopt::sndhwm, opts.snd_hwm);
        m_socket->set(zmq::sockopt::linger, opts.linger_ms);
        m_socket->bind(m_endpoint);
    } catch (const zmq::error_t& e) {
        log_error << "ZmqPublisher bind failed: " << m_endpoint << ", err=" << e.what();
        m_socket.reset();
        return false;
    }

    return true;
}

void ZmqPublisher::Unbind() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_socket) {
        m_socket->close();
        m_socket.reset();
    }
}

bool ZmqPublisher::Publish(const std::string& topic, const std::string& payload)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_socket) {
        log_error << "ZmqPublisher::Publish: not bound";
        return false;
    }

    TransportMessage msg;
    msg.topic   = topic;
    msg.payload = payload;

    std::string encoded;
    if (!m_codec->Encode(msg, encoded)) {
        log_error << "ZmqPublisher encode failed";
        return false;
    }

    try {
        if (!topic.empty()) {
            // multipart: [topic][payload]，与 ZmqSubscriber 的 recv_multipart 对应
            m_socket->send(zmq::buffer(topic), zmq::send_flags::sndmore);
        }
        zmq::message_t data_msg(encoded.size());
        std::memcpy(data_msg.data(), encoded.data(), encoded.size());
        m_socket->send(data_msg, zmq::send_flags::none);
    } catch (const zmq::error_t& e) {
        log_error << "ZmqPublisher send error: " << e.what();
        return false;
    }

    return true;
}
