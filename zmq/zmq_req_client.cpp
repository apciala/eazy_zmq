#include "zmq_req_client.h"

#include <zmq.hpp>

#include "all_include.h"
#include "zmq_context.h"
#include "zmq_codec_passthrough.h"

ZmqReqClient::ZmqReqClient(std::string endpoint, std::shared_ptr<IZmqCodec> codec)
    : m_endpoint(std::move(endpoint))
    , m_codec(codec ? std::move(codec) : std::make_shared<ZmqPassthroughCodec>())
{
}

ZmqReqClient::~ZmqReqClient() noexcept
{
    Disconnect();
}

bool ZmqReqClient::Connect(const ZmqSocketOptions& opts)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_socket) {
        return true;
    }

    try {
        m_socket = std::make_unique<zmq::socket_t>(ZmqContext::Get(), zmq::socket_type::req);
        m_socket->set(zmq::sockopt::sndtimeo, opts.send_timeout_ms);
        m_socket->set(zmq::sockopt::rcvtimeo, opts.recv_timeout_ms);
        m_socket->set(zmq::sockopt::reconnect_ivl, opts.reconnect_ms);
        m_socket->set(zmq::sockopt::sndhwm, opts.snd_hwm);
        m_socket->set(zmq::sockopt::rcvhwm, opts.rcv_hwm);
        m_socket->set(zmq::sockopt::linger, opts.linger_ms);
        m_socket->connect(m_endpoint);
    } catch (const zmq::error_t& e) {
        log_error << "ZmqReqClient connect failed: " << m_endpoint << ", err=" << e.what();
        m_socket.reset();
        return false;
    }

    return true;
}

void ZmqReqClient::Disconnect() noexcept
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_socket) {
        m_socket->close();
        m_socket.reset();
    }
}

std::optional<std::string> ZmqReqClient::Call(const std::string& payload, int timeout_ms)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_socket) {
        log_error << "ZmqReqClient::Call: not connected";
        return std::nullopt;
    }

    // 临时覆盖超时设置
    try {
        m_socket->set(zmq::sockopt::sndtimeo, timeout_ms);
        m_socket->set(zmq::sockopt::rcvtimeo, timeout_ms);
    } catch (const zmq::error_t& e) {
        log_error << "ZmqReqClient set timeout failed: " << e.what();
        return std::nullopt;
    }

    TransportMessage req;
    req.payload = payload;
    std::string encoded;
    if (!m_codec->Encode(req, encoded)) {
        log_error << "ZmqReqClient encode failed";
        return std::nullopt;
    }

    try {
        zmq::message_t req_msg(encoded.size());
        std::memcpy(req_msg.data(), encoded.data(), encoded.size());
        if (!m_socket->send(req_msg, zmq::send_flags::none)) {
            log_error << "ZmqReqClient send failed";
            return std::nullopt;
        }
    } catch (const zmq::error_t& e) {
        log_error << "ZmqReqClient send error: " << e.what();
        return std::nullopt;
    }

    zmq::message_t rep_msg;
    try {
        if (!m_socket->recv(rep_msg, zmq::recv_flags::none)) {
            log_error << "ZmqReqClient recv timeout or failed";
            return std::nullopt;
        }
    } catch (const zmq::error_t& e) {
        log_error << "ZmqReqClient recv error: " << e.what();
        return std::nullopt;
    }

    TransportMessage rep;
    const std::string raw(static_cast<const char*>(rep_msg.data()), rep_msg.size());
    if (!m_codec->Decode(raw, rep)) {
        log_error << "ZmqReqClient decode failed";
        return std::nullopt;
    }

    return rep.payload;
}
