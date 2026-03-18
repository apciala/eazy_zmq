# Eazy ZMQ

基于 ZeroMQ 的 C++17 封装库，简化进程间通信开发。

## 项目结构

```
eazy_zmq/
├── zmq_wrapper/          # 主封装库（推荐使用）
│   ├── zmq_rep_server    # REP 服务端（请求-响应）
│   ├── zmq_req_client    # REQ 客户端
│   ├── zmq_publisher     # PUB 发布端（发布-订阅）
│   ├── zmq_subscriber    # SUB 订阅端
│   ├── zmq_router        # ROUTER 模式（多客户端路由）
│   ├── zmq_dealer        # DEALER 模式
│   ├── DESIGN.md         # 设计文档
│   └── tests/            # 测试用例
│
├── zmq_gateway/          # 网关服务
│   ├── gateway.h/cpp     # 网关实现
│   ├── main.cpp          # 网关主程序
│   └── tests/            # 测试用例
│
├── zmq/                  # 旧版封装（已废弃，不建议使用）
│   └── ...
│
├── zmq_ipc_tester/       # 测试工具
│
└── third_party/          # 第三方依赖
    └── zmq.hpp/          # cppzmq 头文件
```

## 快速开始

### REQ/REP 模式（请求-响应）

```cpp
#include "zmq_wrapper.h"

using namespace zmq_wrap;

// 服务端
ReplierSocket server("ipc:///tmp/test.sock");
server.Bind();
server.SetHandler([](const std::string& req) { 
    return "pong: " + req; 
});
server.StartBackground();

// 客户端
RequesterSocket client("ipc:///tmp/test.sock");
client.Connect();
auto reply = client.Request("ping", 1000);
if (reply) {
    std::cout << "Reply: " << *reply << std::endl;
}
```

### PUB/SUB 模式（发布-订阅）

```cpp
// 发布端
PublisherSocket pub("ipc:///tmp/events.sock");
pub.Bind();
pub.Publish("sensor/temp", R"({"value":36.5})");

// 订阅端
SubscriberSocket sub("ipc:///tmp/events.sock");
sub.Subscribe("sensor/temp", [](const std::string& topic, const std::string& payload) {
    std::cout << "[" << topic << "] " << payload << std::endl;
});
sub.Connect();
sub.StartBackground();
```

### ROUTER/DEALER 模式（多客户端路由）

```cpp
// ROUTER 服务端
RouterSocket router("ipc:///tmp/router.sock");
router.Bind();
router.StartBackground([](const RoutedFrames& msg) {
    std::cout << "From: " << msg.peer_id << " Data: " << msg.frames[0] << std::endl;
    // 路由到其他客户端
    router.SendTo(target_id, "reply message");
});

// DEALER 客户端
SocketConfig cfg;
cfg.identity = "client-001";
DealerSocket dealer("ipc:///tmp/router.sock", cfg);
dealer.Connect();
dealer.Send("Hello");
```

### 网关服务

```cpp
// 启动网关
./zmq_gateway

// 端点：
// - REP:  ipc:///userdata/ipc/zmq_gateway/cmd.sock   (命令注册)
// - ROUTER: ipc:///userdata/ipc/zmq_gateway/router.sock (消息路由)
// - PUB:  ipc:///userdata/ipc/zmq_gateway/pub.sock   (广播通知)

// 客户端注册
{"cmd": "register", "name": "my-service", "identity": "client-001"}

// 消息路由（通过 ROUTER）
{"target": "other-service", "data": "Hello"}
```

## 编译

### 依赖

- CMake 3.14+
- C++17 编译器
- libzmq3-dev

```bash
# Ubuntu
sudo apt-get install libzmq3-dev cmake g++

# 编译
cd zmq_wrapper
mkdir build && cd build
cmake ..
make -j$(nproc)

# 运行测试
./tests/zmq_wrapper_test
```

### 编译网关

```bash
cd zmq_gateway
mkdir build && cd build
cmake ..
make -j$(nproc)

# 运行
./zmq_gateway
```

## 特性

- **线程安全**：所有 socket 操作都有 mutex 保护
- **后台模式**：支持 `StartBackground()` 自动接收循环
- **优雅退出**：Ctrl+C 一次即可退出（使用 poll 超时机制）
- **统计信息**：`GetStats()` 查看 send_ok/recv_ok/eagain 等
- **灵活配置**：SocketConfig 支持 linger/hwm/timeout 等参数

## 文档

- [zmq_wrapper/DESIGN.md](zmq_wrapper/DESIGN.md) - 封装设计说明
- [zmq_gateway/](zmq_gateway/) - 网关服务实现
