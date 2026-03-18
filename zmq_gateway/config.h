#pragma once

#include <string>

namespace zmq_gateway {

/// Gateway 配置
struct GatewayConfig {
    // 默认端点
    std::string rep_endpoint    = "ipc:///userdata/ipc/zmq_gateway/cmd.sock";
    std::string router_endpoint = "ipc:///userdata/ipc/zmq_gateway/router.sock";
    std::string pub_endpoint    = "ipc:///userdata/ipc/zmq_gateway/pub.sock";

    // 从配置文件加载
    bool LoadFromFile(const std::string& path);

    // 从环境变量加载（覆盖默认值）
    void LoadFromEnv();
};

}  // namespace zmq_gateway