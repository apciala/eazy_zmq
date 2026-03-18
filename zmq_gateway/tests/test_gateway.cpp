// zmq_gateway 功能测试
// 测试客户端注册、消息路由、广播通知

#include <chrono>
#include <iostream>
#include <thread>

#include "zmq_wrapper.h"

using namespace zmq_wrap;
using namespace std::chrono_literals;

// 测试 REP socket 注册
void test_register() {
    std::cout << "\n=== Test: Client Registration ===" << std::endl;

    const std::string rep_endpoint = "ipc:///userdata/ipc/zmq_gateway/cmd.sock";

    RequesterSocket client(rep_endpoint);
    if (!client.Connect()) {
        std::cerr << "Failed to connect to REP endpoint" << std::endl;
        return;
    }

    // 注册客户端
    std::string register_cmd = R"({"cmd":"register","name":"client1","identity":"id-client-001"})";
    auto reply = client.Request(register_cmd, 1000);
    if (reply) {
        std::cout << "Register reply: " << *reply << std::endl;
    } else {
        std::cerr << "Register request timeout" << std::endl;
    }

    std::this_thread::sleep_for(100ms);

    // 再次注册（更新）
    reply = client.Request(register_cmd, 1000);
    if (reply) {
        std::cout << "Register again reply: " << *reply << std::endl;
    }

    std::this_thread::sleep_for(100ms);

    // 列出客户端
    reply = client.Request(R"({"cmd":"list"})", 1000);
    if (reply) {
        std::cout << "List clients: " << *reply << std::endl;
    }

    std::this_thread::sleep_for(100ms);

    // 统计信息
    reply = client.Request(R"({"cmd":"stats"})", 1000);
    if (reply) {
        std::cout << "Stats: " << *reply << std::endl;
    }
}

// 测试 ROUTER socket 消息路由
void test_router() {
    std::cout << "\n=== Test: Router Messaging ===" << std::endl;

    const std::string router_endpoint = "ipc:///userdata/ipc/zmq_gateway/router.sock";

    // 创建两个 DEALER 客户端
    SocketConfig cfg1, cfg2;
    cfg1.identity = "id-client-001";  // 对应之前注册的 client1
    cfg2.identity = "id-client-002";

    DealerSocket client1(router_endpoint, cfg1);
    DealerSocket client2(router_endpoint, cfg2);

    if (!client1.Connect()) {
        std::cerr << "Client1 connect failed" << std::endl;
        return;
    }
    if (!client2.Connect()) {
        std::cerr << "Client2 connect failed" << std::endl;
        return;
    }

    // 先注册 client2
    RequesterSocket reg_client("ipc:///userdata/ipc/zmq_gateway/cmd.sock");
    reg_client.Connect();
    reg_client.Request(R"({"cmd":"register","name":"client2","identity":"id-client-002"})", 1000);

    std::this_thread::sleep_for(100ms);

    // client2 启动后台接收
    int recv_count = 0;
    client2.StartBackground([&](const std::string& msg) {
        std::cout << "Client2 received: " << msg << std::endl;
        ++recv_count;
    });

    std::this_thread::sleep_for(100ms);

    // client1 发送消息给 client2
    std::string msg_to_client2 = R"({"target":"client2","data":"Hello from client1"})";
    client1.Send(msg_to_client2);
    std::cout << "Client1 sent: " << msg_to_client2 << std::endl;

    std::this_thread::sleep_for(500ms);

    std::cout << "Client2 received " << recv_count << " messages" << std::endl;

    client2.Stop();
}

// 测试 PUB socket 广播
void test_broadcast() {
    std::cout << "\n=== Test: Broadcast ===" << std::endl;

    const std::string pub_endpoint = "ipc:///userdata/ipc/zmq_gateway/pub.sock";

    SubscriberSocket sub(pub_endpoint);
    if (!sub.Connect()) {
        std::cerr << "Subscriber connect failed" << std::endl;
        return;
    }

    int recv_count = 0;
    sub.Subscribe("register", [&](const std::string& topic, const std::string& payload) {
        std::cout << "Received broadcast [" << topic << "]: " << payload << std::endl;
        ++recv_count;
    });

    sub.StartBackground();
    std::this_thread::sleep_for(200ms);  // 等待订阅生效

    // 注册一个新客户端，应该触发广播
    RequesterSocket client("ipc:///userdata/ipc/zmq_gateway/cmd.sock");
    client.Connect();
    client.Request(R"({"cmd":"register","name":"client3","identity":"id-client-003"})", 1000);

    std::this_thread::sleep_for(500ms);

    std::cout << "Received " << recv_count << " broadcast messages" << std::endl;

    sub.Stop();
}

int main() {
    std::cout << "ZMQ Gateway Test Suite" << std::endl;
    std::cout << "=======================" << std::endl;

    try {
        test_register();
        test_router();
        test_broadcast();

        std::cout << "\n=== All Tests Completed ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}