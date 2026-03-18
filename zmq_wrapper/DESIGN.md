# ZMQ 封装库设计文档

## 一、目标

对 libzmq 做一层通用的、对业务友好的 C++17 封装（基于 cppzmq），解决以下问题：

| 原始 zmq 问题 | 本封装的解法 |
|---|---|
| Context 需要手动管理生命周期 | 进程级全局单例，用户无感知 |
| Socket 类型（PUB/REP/ROUTER 等）差异大，接口碎片化 | 每种角色独立类，接口只暴露该角色合法操作 |
| linger 默认值导致进程退出卡死 | 强制默认 `linger_ms = 0` |
| ROUTER/DEALER 多帧处理容易出错 | `RoutedMessage` 强类型，自动打包/解包路由帧 |
| 错误信息只有 errno | 日志自动带端点、操作名、异常信息 |
| 配置选项散落各处 | 统一 `SocketConfig` 结构体 |

---

## 二、文件结构

```
Code/base/zmq_wrapper/
├── DESIGN.md              ← 本文件
├── zmq_wrapper.h          ← 业务代码只需 include 这一个
├── zmq_options.h          ← SocketConfig, SocketStats, RoutedMessage
├── zmq_context.h          ← ZmqGlobalCtx（内部用，用户不直接使用）
├── zmq_context.cpp
├── zmq_publisher.h        ← PUB socket：发布者
├── zmq_publisher.cpp
├── zmq_subscriber.h       ← SUB socket：订阅者（后台线程接收）
├── zmq_subscriber.cpp
├── zmq_req_client.h       ← REQ socket：同步请求客户端
├── zmq_req_client.cpp
├── zmq_rep_server.h       ← REP socket：回复服务端（支持 tick/后台）
├── zmq_rep_server.cpp
├── zmq_router.h           ← ROUTER socket：多路由服务端
├── zmq_router.cpp
├── zmq_dealer.h           ← DEALER socket：异步工作节点
└── zmq_dealer.cpp
```

---

## 三、核心设计决策

### 3.1 Context 生命周期

```
ZmqGlobalCtx::get()
  └── static ZmqGlobalCtx instance（函数级静态，进程退出时析构）
        └── zmq::context_t ctx_（cppzmq RAII 对象）
```

- 用户代码中**不出现** Context 概念
- 每个 Socket 构造时内部调用 `ZmqGlobalCtx::get()`，返回 `zmq::context_t&`
- 析构顺序：Socket 先析构，Context 后析构（静态对象逆序析构保证）

### 3.2 Socket 角色分类

| 类名 | ZMQ 类型 | bind/connect | send/recv |
|---|---|---|---|
| `zmq_wrap::PublisherSocket` | ZMQ_PUB | bind | send only |
| `zmq_wrap::SubscriberSocket` | ZMQ_SUB | connect | recv only（后台线程） |
| `zmq_wrap::RequesterSocket` | ZMQ_REQ | connect | request()（原子 send+recv） |
| `zmq_wrap::ReplierSocket` | ZMQ_REP | bind | PollOnce() 或后台线程 |
| `zmq_wrap::RouterSocket` | ZMQ_ROUTER | bind | recv()->RoutedMessage, SendTo() |
| `zmq_wrap::DealerSocket` | ZMQ_DEALER | connect | send() + recv() |

### 3.3 linger 安全默认值

```cpp
struct SocketConfig {
    int linger_ms = 0;  // 强制默认：关闭时立即丢弃，不阻塞
    // ...
};
```

原因：libzmq 默认 linger=-1（无限等待），若对端不可达，`zmq_ctx_destroy()` 会永久阻塞进程。
默认改为 0，需要可靠传递时业务显式设置。

### 3.4 线程安全

- **ReplierSocket**：`m_sock` 为 `shared_ptr<zmq::socket_t>`，`PollOnce()` 锁内拷贝副本到栈，锁外使用副本进行 poll/recv/send，与 `Stop()` 并发安全
- **SubscriberSocket/DealerSocket**：`m_sock` 为 `optional<zmq::socket_t>`，后台线程 `RecvLoop` 持锁访问，`Disconnect()` 先 join 线程再 reset socket
- **其他类**：方法内部全程持 mutex，调用方保证同一时刻只有一个线程调用即可

### 3.5 ROUTER/DEALER 多帧

DEALER/ROUTER 直连帧格式（**无 delimiter 空帧**，delimiter 只属于 REQ/REP 信封）：

```
DEALER → ROUTER 方向：
  DEALER.Send(payload)       → 线路上：[identity][payload]  (ZMQ 自动插入 identity)
  RouterSocket.Recv()        → 解包出：RoutedMessage{peer_id, payload}

ROUTER → DEALER 方向：
  RouterSocket.SendTo(id, p) → 线路上：[identity][payload]
  DEALER.Recv()              → 得到：payload  (ZMQ 自动剥离 identity)
```

封装层自动处理，用户只看到：

```cpp
struct RoutedMessage {
    std::string peer_id;  // identity 帧内容
    std::string payload;  // 业务数据
};
```

### 3.6 错误处理策略

| 场景 | 处理方式 |
|---|---|
| Bind/Connect 失败 | `log_error` + 返回 `false`（不抛异常）|
| send 失败（EAGAIN/其他）| `log_error` + 返回 `false` |
| recv 超时（EAGAIN） | 返回 `std::nullopt`（正常情况）|
| recv 其他错误 | `log_error` + 返回 `std::nullopt` |
| ETERM（context 终止） | `log_info` + 静默退出循环 |
| StartBackground() 重复调用 | `log_warn` + 返回 `false` |
| StartBackground() 未 Bind/Connect | `log_error` + 返回 `false` |

### 3.7 日志规范

- 所有日志前缀：`[ZMQ]`，便于 `grep`
- 使用项目 logger 宏：`log_debug`, `log_info`, `log_warn`, `log_error`
- 关键操作日志字段：`endpoint=`, `error=`, `peer_id=`（ROUTER）

---

## 四、与旧封装（zmq/ 目录）对比

| 维度 | 旧封装（zmq/） | 新封装（zmq_wrapper/）|
|---|---|---|
| Context | 全局单例（zmq.hpp） | 全局单例（zmq.hpp，返回 `zmq::context_t&`）|
| API 依赖 | cppzmq（zmq.hpp） | cppzmq（zmq.hpp）|
| 编解码 | IZmqCodec 接口（JSON/passthrough）| 无（业务层自行序列化）|
| ROUTER/DEALER | 不支持 | 支持，RoutedMessage 强类型 |
| 线程安全 | 无特殊保护 | ReplierSocket 用 shared_ptr 副本，其他用 mutex |
| 诊断统计 | 无 | SocketStats，可通过 GetStats() 查询 |
| linger 默认 | 0（已安全）| 0（保持安全默认）|
| 配置方式 | ZmqSocketOptions（Start/Bind 时传入）| SocketConfig（相同模式）|
| 后台线程 | REP/SUB 支持 | REP/SUB/DEALER 支持，StartBackground() 返回 bool |

---

## 五、典型用法

### PUB/SUB

```cpp
// 发布端
zmq_wrap::PublisherSocket pub("ipc:///userdata/ipc/events.sock");
pub.Bind();
pub.Publish("sensor/temp", "36.5");

// 订阅端
zmq_wrap::SubscriberSocket sub("ipc:///userdata/ipc/events.sock");
sub.Connect();
sub.Subscribe("sensor/temp", [](const std::string& topic, const std::string& payload) {
    log_info << "recv: " << topic << " => " << payload;
});
if (!sub.StartBackground()) {
    log_error << "StartBackground failed";
}
// ... 业务运行 ...
sub.Stop();
```

### REQ/REP

```cpp
// 服务端
zmq_wrap::ReplierSocket rep("ipc:///userdata/ipc/cmd.sock");
rep.Bind();
rep.SetHandler([](const std::string& req) -> std::string {
    return "pong:" + req;
});
if (!rep.StartBackground()) {
    log_error << "StartBackground failed";
}

// 客户端（原子 send+recv，REQ/REP 顺序自动保证）
zmq_wrap::RequesterSocket req("ipc:///userdata/ipc/cmd.sock");
req.Connect();
auto reply = req.Request("ping", 3000);
if (reply) log_info << "reply: " << *reply;
```

### ROUTER/DEALER

```cpp
// ROUTER 服务端
zmq_wrap::RouterSocket router("tcp://*:5555");
router.Bind();
while (running) {
    auto msg = router.Recv(100);  // timeout_ms=100
    if (!msg) continue;
    log_info << "from peer=" << msg->peer_id << " payload=" << msg->payload;
    router.SendTo(msg->peer_id, "pong:" + msg->payload);
}

// DEALER 客户端
zmq_wrap::DealerSocket dealer("tcp://localhost:5555",
                               SocketConfig{.identity = "worker-01"});
dealer.Connect();
dealer.Send("ping");
auto reply = dealer.Recv(1000);
```
