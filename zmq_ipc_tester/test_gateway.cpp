#include <chrono>
#include <iostream>
#include <thread>
#include <json/json.h>
#include <zmq.hpp>

// Gateway 端点
constexpr const char* kRepEndpoint    = "ipc:///userdata/ipc/zmq_gateway/cmd.sock";
constexpr const char* kSubEndpoint    = "ipc:///userdata/ipc/zmq_gateway/events.sock";
constexpr const char* kRouterEndpoint = "ipc:///userdata/ipc/zmq_gateway/router.sock";

void TestRepCommand() {
    std::cout << "\n=== Test 1: REP Command Channel ===\n";

    zmq::context_t ctx(1);
    zmq::socket_t req(ctx, zmq::socket_type::req);
    req.connect(kRepEndpoint);

    // 发送 register 命令
    Json::Value cmd;
    cmd["cmd"] = "register";
    cmd["name"] = "clientA";
    cmd["identity"] = "test-client-A";

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    std::string payload = Json::writeString(writer, cmd);

    std::cout << "Sending: " << payload << "\n";
    req.send(zmq::buffer(payload), zmq::send_flags::none);

    zmq::message_t reply;
    auto result = req.recv(reply, zmq::recv_flags::none);
    if (result) {
        std::string resp(static_cast<char*>(reply.data()), reply.size());
        std::cout << "Received: " << resp << "\n";
    } else {
        std::cout << "No reply received\n";
    }

    // 发送 status 命令
    cmd.clear();
    cmd["cmd"] = "status";
    payload = Json::writeString(writer, cmd);

    std::cout << "Sending: " << payload << "\n";
    req.send(zmq::buffer(payload), zmq::send_flags::none);

    result = req.recv(reply, zmq::recv_flags::none);
    if (result) {
        std::string resp(static_cast<char*>(reply.data()), reply.size());
        std::cout << "Received: " << resp << "\n";
    }
}

void TestSubEvents() {
    std::cout << "\n=== Test 2: SUB Event Channel ===\n";
    std::cout << "Subscribing to alarm and sensor topics for 5 seconds...\n";

    zmq::context_t ctx(1);
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.connect(kSubEndpoint);
    sub.set(zmq::sockopt::subscribe, "alarm");
    sub.set(zmq::sockopt::subscribe, "sensor");
    sub.set(zmq::sockopt::rcvtimeo, 1000);

    auto start = std::chrono::steady_clock::now();
    int count = 0;

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= 5) break;

        zmq::message_t topic_msg;
        auto r1 = sub.recv(topic_msg, zmq::recv_flags::none);
        if (!r1) continue;

        std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());

        zmq::message_t payload_msg;
        sub.recv(payload_msg, zmq::recv_flags::none);
        std::string payload(static_cast<char*>(payload_msg.data()), payload_msg.size());

        std::cout << "Received event: topic=" << topic << " payload=" << payload << "\n";
        ++count;
    }

    std::cout << "Total events received: " << count << "\n";
}

void TestRouterMessaging() {
    std::cout << "\n=== Test 3: ROUTER 4-Frame Protocol ===\n";

    zmq::context_t ctx(1);
    zmq::socket_t dealer(ctx, zmq::socket_type::dealer);
    dealer.set(zmq::sockopt::routing_id, "clientA");
    dealer.connect(kRouterEndpoint);

    // 发送 4 帧消息：[target_name][空帧][payload]
    // ROUTER 收到后会变成：[src_identity][target_name][空帧][payload]
    std::string target = "clientB";
    std::string delimiter = "";
    std::string payload = "Hello from clientA";

    std::cout << "Sending 3-frame message to clientB:\n";
    std::cout << "  Frame 1: target=" << target << "\n";
    std::cout << "  Frame 2: delimiter (empty)\n";
    std::cout << "  Frame 3: payload=" << payload << "\n";

    dealer.send(zmq::buffer(target), zmq::send_flags::sndmore);
    dealer.send(zmq::buffer(delimiter), zmq::send_flags::sndmore);
    dealer.send(zmq::buffer(payload), zmq::send_flags::none);

    // 等待回复（如果有其他客户端的话）
    dealer.set(zmq::sockopt::rcvtimeo, 2000);
    zmq::message_t msg1, msg2, msg3;

    auto r1 = dealer.recv(msg1, zmq::recv_flags::none);
    if (r1) {
        dealer.recv(msg2, zmq::recv_flags::none);
        dealer.recv(msg3, zmq::recv_flags::none);

        std::string src(static_cast<char*>(msg1.data()), msg1.size());
        std::string delim(static_cast<char*>(msg2.data()), msg2.size());
        std::string data(static_cast<char*>(msg3.data()), msg3.size());

        std::cout << "Received reply:\n";
        std::cout << "  Frame 1: src=" << src << "\n";
        std::cout << "  Frame 2: delimiter=" << (delim.empty() ? "(empty)" : delim) << "\n";
        std::cout << "  Frame 3: data=" << data << "\n";
    } else {
        std::cout << "No reply (no other clients connected)\n";
    }
}

void TestPublishEvents() {
    std::cout << "\n=== Test 4: Publish Events (for SUB to receive) ===\n";

    zmq::context_t ctx(1);
    zmq::socket_t pub(ctx, zmq::socket_type::pub);
    pub.bind(kSubEndpoint);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));  // 等待订阅者连接

    for (int i = 0; i < 3; ++i) {
        Json::Value event;
        event["type"] = "alarm";
        event["level"] = "warning";
        event["message"] = "Test alarm " + std::to_string(i);

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        std::string payload = Json::writeString(writer, event);

        std::cout << "Publishing alarm: " << payload << "\n";
        pub.send(zmq::buffer("alarm"), zmq::send_flags::sndmore);
        pub.send(zmq::buffer(payload), zmq::send_flags::none);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    for (int i = 0; i < 2; ++i) {
        Json::Value event;
        event["type"] = "sensor";
        event["value"] = 25.5 + i;

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        std::string payload = Json::writeString(writer, event);

        std::cout << "Publishing sensor: " << payload << "\n";
        pub.send(zmq::buffer("sensor"), zmq::send_flags::sndmore);
        pub.send(zmq::buffer(payload), zmq::send_flags::none);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: " << argv[0] << " [test_number]\n"
                  << "  1: Test REP command channel\n"
                  << "  2: Test SUB event channel (requires publisher)\n"
                  << "  3: Test ROUTER 4-frame protocol\n"
                  << "  4: Publish test events (run in separate terminal)\n"
                  << "  (no args): Run all tests\n";
        return 0;
    }

    int test = 0;
    if (argc > 1) {
        test = std::atoi(argv[1]);
    }

    try {
        if (test == 0 || test == 1) TestRepCommand();
        if (test == 0 || test == 3) TestRouterMessaging();
        if (test == 2) TestSubEvents();
        if (test == 4) TestPublishEvents();

        std::cout << "\n=== All tests completed ===\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
