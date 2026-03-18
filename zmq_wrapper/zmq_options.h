#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zmq_wrap {

/// 所有 socket 类型共用的配置项。
/// 关键默认值：linger_ms = 0，确保进程关闭时不阻塞。
struct SocketConfig {
    int         linger_ms       = 0;     ///< 关闭时消息保留（ms），0=立即丢弃，防止 ctx_destroy 卡死
    int         send_timeout_ms = -1;    ///< 发送超时（ms），-1=永久阻塞
    int         recv_timeout_ms = -1;    ///< 接收超时（ms），-1=永久阻塞
    int         send_hwm        = 1000;  ///< 发送高水位（消息条数），超过后 send 阻塞或丢弃
    int         recv_hwm        = 1000;  ///< 接收高水位（消息条数）
    std::string identity;                ///< DEALER/ROUTER 用，空=ZMQ 自动生成
};

/// 运行时收发统计，通过 GetStats() 查询。
struct SocketStats {
    uint64_t send_ok   = 0;  ///< 成功发送次数
    uint64_t recv_ok   = 0;  ///< 成功接收次数
    uint64_t send_fail = 0;  ///< 发送失败次数（含超时）
    uint64_t recv_fail = 0;  ///< 接收失败次数（不含 EAGAIN）
    uint64_t eagain    = 0;  ///< recv 返回 EAGAIN 次数（暂无数据/超时），用于判断背压
};

/// ROUTER/DEALER 模式下带路由信息的消息（单帧 payload）。
/// RouterSocket::Recv() 返回此类型；RouterSocket::SendTo() 消费 peer_id。
struct RoutedMessage {
    std::string peer_id;  ///< 对端 identity 帧（ZMQ 路由信息，勿修改后再 SendTo）
    std::string payload;  ///< 业务数据，单帧
};

/// ROUTER/DEALER 模式下多帧消息（RecvAll 返回）。
/// frames 包含 identity 之后的所有帧，顺序与发送端一致。
struct RoutedFrames {
    std::string              peer_id;  ///< 对端 identity 帧
    std::vector<std::string> frames;   ///< payload 帧列表（>=1）
};

}  // namespace zmq_wrap

namespace zmq_wrap::detail {
/// Background thread poll interval (ms). Controls how quickly Stop()/Disconnect()
/// returns after setting m_running = false. Lower = faster shutdown, higher CPU.
inline constexpr int kPollTimeoutMs = 100;
}  // namespace zmq_wrap::detail
