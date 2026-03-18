# Eazy ZMQ

基于 ZeroMQ 的 IPC 通信封装库，简化进程间通信开发。

## 项目结构

```
eazy_zmq/
├── zmq/                  # 核心 ZMQ 封装
│   ├── zmq_rep_server    # REP 服务端（请求-响应）
│   ├── zmq_req_client    # REQ 客户端
│   ├── zmq_publisher     # PUB 发布端（发布-订阅）
│   ├── zmq_subscriber    # SUB 订阅端
│   └── zmq_codec_*       # 编解码器（JSON/Passthrough）
│
├── zmq_wrapper/          # 扩展封装
│   ├── zmq_router        # ROUTER 模式
│   ├── zmq_dealer        # DEALER 模式
│   └── DESIGN.md         # 设计文档
│
├── zmq_gateway/          # 网关服务示例
│   └── main.cpp          # 网关主程序
│
├── zmq_ipc_tester/       # 测试工具
│
└── third_party/          # 第三方依赖
    └── zmq.hpp           # cppzmq 头文件
```

## 快速开始

### REQ/REP 模式（请求-响应）

```cpp
// 服务端
auto server = std::make_shared<ZmqRepServer>("ipc:///tmp/test.sock");
server->Start();
server->SetHandler([](const std::string& req) { return "pong"; });
server->StartBackground();

// 客户端
ZmqReqClient client("ipc:///tmp/test.sock");
client.Connect();
auto reply = client.Call(R"({"cmd":"ping"})");
```

### PUB/SUB 模式（发布-订阅）

```cpp
// 发布端
ZmqPublisher pub("ipc:///tmp/events.sock");
pub.Bind();
pub.Publish("sensor/temp", R"({"value":36.5})");

// 订阅端
ZmqSubscriber sub("ipc:///tmp/events.sock");
sub.Subscribe("sensor/temp", [](auto topic, auto payload) {
    std::cout << payload << std::endl;
});
sub.Connect();
sub.StartBackground();
```

## 编译

```bash
mkdir build && cd build
cmake ..
make
```

## 文档

- [zmq/README.md](zmq/README.md) - 核心封装详细使用文档
- [zmq_wrapper/DESIGN.md](zmq_wrapper/DESIGN.md) - 设计说明