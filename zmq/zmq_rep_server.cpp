#include "zmq_rep_server.h"

#include <zmq.hpp>
#include <zmq.h>

#include "all_include.h"
#include "zmq_context.h"
#include "zmq_codec_passthrough.h"

ZmqRepServer::ZmqRepServer(std::string endpoint, std::shared_ptr<IZmqCodec> codec)
    : m_endpoint(std::move(endpoint))
    , m_codec(codec ? std::move(codec) : std::make_shared<ZmqPassthroughCodec>())
{
}

ZmqRepServer::~ZmqRepServer() noexcept
{
    Stop();
}

bool ZmqRepServer::Start(const ZmqSocketOptions& opts)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_running) {
        return true;
    }

    try {
        auto sock = std::make_shared<zmq::socket_t>(ZmqContext::Get(), zmq::socket_type::rep);
        ApplyOptions(opts);  // opts 在 make_shared 之后，先保存再用
        // 重新用局部 sock 应用选项
        sock->set(zmq::sockopt::sndtimeo, opts.send_timeout_ms);
        sock->set(zmq::sockopt::rcvtimeo, opts.recv_timeout_ms);
        sock->set(zmq::sockopt::reconnect_ivl, opts.reconnect_ms);
        sock->set(zmq::sockopt::sndhwm, opts.snd_hwm);
        sock->set(zmq::sockopt::rcvhwm, opts.rcv_hwm);
        sock->set(zmq::sockopt::linger, opts.linger_ms);
        sock->bind(m_endpoint);
        m_socket = std::move(sock);
    } catch (const zmq::error_t& e) {
        log_error << "ZmqRepServer bind failed: " << m_endpoint << ", err=" << e.what();
        return false;
    }

    m_running = true;
    return true;
}

void ZmqRepServer::Stop() noexcept
{
    m_running = false;

    if (m_thread.joinable()) {
        m_thread.join();
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_socket.reset();  // context 仍存活，reset socket 即可
}

void ZmqRepServer::SetHandler(Handler handler)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_handler = std::move(handler);
}

bool ZmqRepServer::PollOnce(int timeout_ms)
{
    // 锁内取局部副本，锁外操作，避免与 Stop() 竞争
    std::shared_ptr<zmq::socket_t> sock;
    Handler handler;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_running || !m_socket) {
            return false;
        }
        sock    = m_socket;
        handler = m_handler;
    }

    if (!handler) {
        return false;
    }

    zmq_pollitem_t items[] = { {sock->handle(), 0, ZMQ_POLLIN, 0} };
    const int rc = zmq_poll(items, 1, timeout_ms);
    if (rc <= 0 || (items[0].revents & ZMQ_POLLIN) == 0) {
        return false;
    }

    zmq::message_t req_msg;
    try {
        if (!sock->recv(req_msg, zmq::recv_flags::none)) {
            return false;
        }
    } catch (const zmq::error_t& e) {
        // ETERM: context 被 Stop() 终止，正常退出
        if (e.num() != ETERM) {
            log_error << "ZmqRepServer recv error: " << e.what();
        }
        return false;
    }

    TransportMessage req;
    const std::string raw(static_cast<const char*>(req_msg.data()), req_msg.size());
    if (!m_codec->Decode(raw, req)) {
        log_error << "ZmqRepServer decode failed";
        return false;
    }

    const std::string reply_payload = handler(req.payload);

    TransportMessage rep;
    rep.payload = reply_payload;
    std::string encoded;
    if (!m_codec->Encode(rep, encoded)) {
        log_error << "ZmqRepServer encode reply failed";
        return false;
    }

    try {
        zmq::message_t rep_msg(encoded.size());
        std::memcpy(rep_msg.data(), encoded.data(), encoded.size());
        sock->send(rep_msg, zmq::send_flags::none);
    } catch (const zmq::error_t& e) {
        log_error << "ZmqRepServer send reply error: " << e.what();
        return false;
    }

    return true;
}

void ZmqRepServer::StartBackground()
{
    m_thread = std::thread(&ZmqRepServer::BackgroundLoop, this);
}

void ZmqRepServer::BackgroundLoop()
{
    while (m_running) {
        PollOnce(100);
    }
}

void ZmqRepServer::ApplyOptions(const ZmqSocketOptions& /*opts*/)
{
    // 选项直接在 Start() 里通过局部 sock 应用，此函数保留供子类扩展
}
