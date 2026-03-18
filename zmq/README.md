# ZMQ 封装使用文档

## 概览

`base/zmq/` 提供四个独立的通信类，覆盖 ZeroMQ 的主要使用场景：

| 类 | 模式 | 典型用途 |
|---|---|---|
| `ZmqRepServer` | REP 服务端 | 接收请求、返回响应 |
| `ZmqReqClient` | REQ 客户端 | 发送请求、等待响应 |
| `ZmqPublisher` | PUB 发布端 | 广播事件/数据 |
| `ZmqSubscriber` | SUB 订阅端 | 订阅并接收事件 |

所有类共享同一个全局 `zmq::context_t`（`ZmqContext::Get()`），符合 ZMQ 官方推荐做法。

---

## 公共配置：ZmqSocketOptions

```cpp
struct ZmqSocketOptions {
    int send_timeout_ms = 3000;  // 发送超时
    int recv_timeout_ms = 3000;  // 接收超时
    int reconnect_ms    = 1000;  // 断线重连间隔
    int snd_hwm         = 1000;  // 发送高水位（消息队列上限）
    int rcv_hwm         = 1000;  // 接收高水位
    int linger_ms       = 0;     // 关闭时等待发送完成的时间（0=立即丢弃）
};
```

从配置文件加载：

```cpp
ZmqSocketOptions opts;
LoadZmqOptionsForCurrentDevice(opts);  // 从设备自身配置文件读取
// 或
LoadZmqOptionsFromSelfSettings(cfg, opts);  // 从已打开的 cconfig 读取
```

---

## Codec（序列化）

### ZmqPassthroughCodec（默认）

不做任何序列化，payload 原样收发。适合调用方自己管理格式的场景。

```cpp
// 所有类的 codec 参数默认为 nullptr，即自动使用 ZmqPassthroughCodec
auto server = std::make_shared<ZmqRepServer>("ipc:///tmp/test.sock");
```

### ZmqJsonCodec

- Encode：payload 是合法 JSON → 紧凑序列化；否则包成 `{"data":"..."}`
- Decode：解析 JSON，失败时原样放入 payload（不丢弃消息）

```cpp
auto server = std::make_shared<ZmqRepServer>(
    "ipc:///tmp/test.sock",
    std::make_shared<ZmqJsonCodec>());
```

### 自定义 Codec

实现 `IZmqCodec` 接口即可（protobuf、msgpack 等）：

```cpp
class MyCodec : public IZmqCodec {
public:
    bool Encode(const TransportMessage& msg, std::string& out) override { ... }
    bool Decode(const std::string& in, TransportMessage& msg) override { ... }
};
```

---

## ZmqRepServer — REP 服务端

### 功能

- bind 到指定 endpoint，等待 REQ 客户端连接
- 收到请求后调用 Handler，将返回值作为响应发回
- 支持两种驱动模式：**tick 驱动**（单线程循环）和**后台线程**

### tick 驱动模式（推荐用于 Server::OnTickServer）

```cpp
// 初始化
auto server = std::make_shared<ZmqRepServer>(
    "ipc:///userdata/ipc/myservice/reqrep.sock",
    std::make_shared<ZmqJsonCodec>());

ZmqSocketOptions opts;
opts.send_timeout_ms = 1000;
opts.recv_timeout_ms = 1000;
server->Start(opts);

server->SetHandler([](const std::string& payload) -> std::string {
    // 处理请求，返回响应
    return R"({"ok":true})";
});

// 在 OnTickServer() 里调用，timeout_ms=5 表示最多等 5ms
int OnTickServer() {
    server->PollOnce(5);
    return 0;
}
```

### 后台线程模式

```cpp
server->Start(opts);
server->SetHandler([](const std::string& payload) -> std::string {
    return "pong";
});
server->StartBackground();  // 启动内部线程，自动循环收发

// 停止时
server->Stop();  // 析构时也会自动调用
```

---

## ZmqReqClient — REQ 客户端

### 功能

- connect 到 REP 服务端
- 同步调用：发送请求，阻塞等待响应，超时返回 `std::nullopt`
- 内部 mutex 保护，多线程安全

### 用法

```cpp
ZmqReqClient client(
    "ipc:///userdata/ipc/myservice/reqrep.sock",
    std::make_shared<ZmqJsonCodec>());

ZmqSocketOptions opts;
opts.send_timeout_ms = 3000;
opts.recv_timeout_ms = 3000;
client.Connect(opts);

// 同步调用
auto reply = client.Call(R"({"cmd":"ping"})", 3000);
if (reply) {
    log_info << "reply: " << *reply;
} else {
    log_error << "call timeout or failed";
}

// 断开
client.Disconnect();  // 析构时也会自动调用
```

---

## ZmqPublisher — PUB 发布端

### 功能

- bind 到指定 endpoint，向所有订阅者广播消息
- 发布时指定 topic，订阅者按 topic 过滤
- 线程安全，可多线程调用 `Publish()`

### 用法

```cpp
ZmqPublisher pub("ipc:///userdata/ipc/events/pubsub.sock");

ZmqSocketOptions opts;
pub.Bind(opts);

// 发布消息（topic + payload）
pub.Publish("sensor/temperature", R"({"value":36.5,"unit":"C"})");
pub.Publish("sensor/humidity",    R"({"value":60})");

// topic 为空时退化为单帧发送（订阅方需订阅空 topic）
pub.Publish("", "raw data");
```

---

## ZmqSubscriber — SUB 订阅端

### 功能

- connect 到 PUB 端
- 按 topic 注册回调，后台线程自动接收并分发
- 支持精确 topic 匹配和通配（空字符串 = 接收所有）
- 可在 `Connect()` 前或后调用 `Subscribe()`

### 用法

```cpp
ZmqSubscriber sub("ipc:///userdata/ipc/events/pubsub.sock");

// 注册 topic 回调（在后台线程中调用，注意线程安全）
sub.Subscribe("sensor/temperature", [](const std::string& topic, const std::string& payload) {
    log_info << "temp: " << payload;
});

sub.Subscribe("sensor/humidity", [](const std::string& topic, const std::string& payload) {
    log_info << "humidity: " << payload;
});

// 订阅所有 topic（通配）
sub.Subscribe("", [](const std::string& topic, const std::string& payload) {
    log_info << "topic=" << topic << " payload=" << payload;
});

ZmqSocketOptions opts;
sub.Connect(opts);
sub.StartBackground();  // 启动后台接收线程

// 停止
sub.Stop();  // 析构时也会自动调用
```

---

## 典型组合场景

### 场景一：微服务 IPC（REQ/REP）

```
设备主进程                    微服务进程
ZmqReqClient  ──请求──►  ZmqRepServer
              ◄──响应──
```

```cpp
// 微服务进程（服务端）
ZmqRepServer server("ipc:///userdata/ipc/mycomp/reqrep.sock");
server.Start();
server.SetHandler([](const std::string& req) {
    return process(req);
});
server.StartBackground();

// 主进程（客户端）
ZmqReqClient client("ipc:///userdata/ipc/mycomp/reqrep.sock");
client.Connect();
auto result = client.Call(R"({"cmd":"getStatus"})");
```

### 场景二：事件总线（PUB/SUB）

```
数据采集模块                   多个消费模块
ZmqPublisher  ──事件──►  ZmqSubscriber A（sensor/temp）
                    ──►  ZmqSubscriber B（sensor/humidity）
                    ──►  ZmqSubscriber C（所有事件）
```

```cpp
// 发布端（数据采集）
ZmqPublisher pub("ipc:///userdata/ipc/bus/pubsub.sock");
pub.Bind();
pub.Publish("sensor/temp", data);

// 订阅端（消费模块）
ZmqSubscriber sub("ipc:///userdata/ipc/bus/pubsub.sock");
sub.Subscribe("sensor/temp", onTemp);
sub.Connect();
sub.StartBackground();
```

---

## 注意事项

1. **REQ/REP 严格交替**：REQ 发送后必须等到 REP 响应才能发下一条，`ZmqReqClient::Call()` 已封装此语义。
2. **PUB/SUB 慢订阅问题**：PUB 在 SUB 连接前发出的消息会丢失，这是 ZMQ 的正常行为。
3. **后台线程回调线程安全**：`ZmqSubscriber` 的 handler 在后台线程调用，访问共享数据需加锁。
4. **endpoint 格式**：
   - IPC（进程间）：`ipc:///path/to/socket.sock`
   - TCP：`tcp://127.0.0.1:5555`
5. **IPC 目录需提前创建**：`sg_system("mkdir -p /userdata/ipc/mycomp")`
