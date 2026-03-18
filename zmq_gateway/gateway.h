#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "config.h"
#include "zmq_options.h"  // for RoutedFrames

// 前向声明，避免暴露 zmq_wrapper 内部细节
namespace zmq_wrap {
    class ReplierSocket;
    class RouterSocket;
    class PublisherSocket;
}

namespace zmq_gateway {

/// 客户端注册信息
struct ClientInfo {
    std::string name;      // 客户端名称
    std::string identity;  // ZMQ identity（用于 ROUTER 路由）
};

/// ZMQ Gateway 网关服务
///
/// 功能：
/// - REP socket：接收客户端注册请求
/// - ROUTER socket：客户端间消息路由
/// - PUB socket：广播通知
class Gateway {
public:
    explicit Gateway(const GatewayConfig& config);
    ~Gateway() noexcept;

    Gateway(const Gateway&) = delete;
    Gateway& operator=(const Gateway&) = delete;

    /// 启动网关服务
    bool Start();

    /// 停止网关服务
    void Stop() noexcept;

    /// 检查是否运行中
    bool IsRunning() const { return m_running; }

    /// 获取已注册客户端数量
    size_t GetClientCount() const;

    /// 获取统计信息
    struct Stats {
        uint64_t registrations = 0;    // 注册次数
        uint64_t messages_routed = 0;  // 路由消息数
        uint64_t broadcasts = 0;       // 广播次数
        uint64_t errors = 0;           // 错误次数
    };
    Stats GetStats() const;

private:
    /// 处理 REP 请求（注册）
    std::string HandleRepRequest(const std::string& payload);

    /// 处理 ROUTER 消息（路由）
    void HandleRouterMessage(const zmq_wrap::RoutedFrames& msg);

    /// 注册客户端
    bool RegisterClient(const std::string& name, const std::string& identity);

    /// 根据 name 查找 identity
    std::string FindClientIdentity(const std::string& name) const;

    /// 广播通知
    void Broadcast(const std::string& topic, const std::string& message);

private:
    GatewayConfig m_config;
    std::atomic<bool> m_running{false};

    // ZMQ sockets
    std::unique_ptr<zmq_wrap::ReplierSocket> m_rep;
    std::unique_ptr<zmq_wrap::RouterSocket> m_router;
    std::unique_ptr<zmq_wrap::PublisherSocket> m_pub;

    // 客户端注册表：name -> identity
    mutable std::mutex m_clients_mutex;
    std::unordered_map<std::string, std::string> m_clients;

    // 统计
    mutable std::mutex m_stats_mutex;
    Stats m_stats;
};

}  // namespace zmq_gateway