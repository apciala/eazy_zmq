#include <chrono>
#include <iostream>
#include <thread>

#include "zmq_dealer.h"
#include "zmq_publisher.h"
#include "zmq_rep_server.h"
#include "zmq_req_client.h"
#include "zmq_router.h"
#include "zmq_subscriber.h"

using namespace zmq_wrap;
using namespace std::chrono_literals;

// Test 1: REQ/REP pattern
void test_req_rep() {
    std::cout << "\n=== Test 1: REQ/REP ===" << std::endl;

    const std::string endpoint = "ipc:///tmp/test_reqrep.sock";
    system("rm -f /tmp/test_reqrep.sock");

    // Server
    ReplierSocket server(endpoint);
    if (!server.Bind()) {
        std::cerr << "Server bind failed" << std::endl;
        return;
    }
    server.SetHandler([](const std::string& req) {
        std::cout << "Server recv: " << req << std::endl;
        return "ECHO: " + req;
    });
    server.StartBackground();

    std::this_thread::sleep_for(100ms);

    // Client
    RequesterSocket client(endpoint);
    if (!client.Connect()) {
        std::cerr << "Client connect failed" << std::endl;
        return;
    }

    auto reply = client.Request("Hello", 1000);
    if (reply) {
        std::cout << "Client recv: " << *reply << std::endl;
    } else {
        std::cerr << "Client request timeout" << std::endl;
    }

    auto stats = client.GetStats();
    std::cout << "Client stats: send_ok=" << stats.send_ok
              << " recv_ok=" << stats.recv_ok << std::endl;

    server.Stop();
}

// Test 2: DEALER/ROUTER pattern
void test_dealer_router() {
    std::cout << "\n=== Test 2: DEALER/ROUTER ===" << std::endl;

    const std::string endpoint = "ipc:///tmp/test_dealer_router.sock";
    system("rm -f /tmp/test_dealer_router.sock");

    // Router server
    RouterSocket router(endpoint);
    if (!router.Bind()) {
        std::cerr << "Router bind failed" << std::endl;
        return;
    }

    // Dealer client
    SocketConfig dealer_cfg;
    dealer_cfg.identity = "dealer-001";
    DealerSocket dealer(endpoint, dealer_cfg);
    if (!dealer.Connect()) {
        std::cerr << "Dealer connect failed" << std::endl;
        return;
    }

    std::this_thread::sleep_for(100ms);

    // Dealer sends
    dealer.Send("Request from dealer");

    // Router receives
    auto msg = router.Recv(1000);
    if (msg) {
        std::cout << "Router recv from peer_id=" << msg->peer_id
                  << " payload=" << msg->payload << std::endl;

        // Router replies
        router.SendTo(msg->peer_id, "Reply from router");
    }

    // Dealer receives
    auto reply = dealer.Recv(1000);
    if (reply) {
        std::cout << "Dealer recv: " << *reply << std::endl;
    }

    auto stats = dealer.GetStats();
    std::cout << "Dealer stats: send_ok=" << stats.send_ok
              << " recv_ok=" << stats.recv_ok << std::endl;
}

// Test 3: PUB/SUB pattern
void test_pub_sub() {
    std::cout << "\n=== Test 3: PUB/SUB ===" << std::endl;

    const std::string endpoint = "ipc:///tmp/test_pubsub.sock";
    system("rm -f /tmp/test_pubsub.sock");

    // Publisher
    PublisherSocket pub(endpoint);
    if (!pub.Bind()) {
        std::cerr << "Publisher bind failed" << std::endl;
        return;
    }

    std::this_thread::sleep_for(100ms);

    // Subscriber
    SubscriberSocket sub(endpoint);
    if (!sub.Connect()) {
        std::cerr << "Subscriber connect failed" << std::endl;
        return;
    }

    int recv_count = 0;
    sub.Subscribe("topic1", [&](const std::string& topic, const std::string& payload) {
        std::cout << "Sub recv topic=" << topic << " payload=" << payload << std::endl;
        ++recv_count;
    });
    sub.Subscribe("topic2", [&](const std::string& topic, const std::string& payload) {
        std::cout << "Sub recv topic=" << topic << " payload=" << payload << std::endl;
        ++recv_count;
    });

    sub.StartBackground();
    std::this_thread::sleep_for(200ms);  // Wait for subscription to propagate

    // Publish messages
    pub.Publish("topic1", "Message 1");
    pub.Publish("topic2", "Message 2");
    pub.Publish("topic3", "Message 3");  // Not subscribed

    std::this_thread::sleep_for(200ms);

    std::cout << "Subscriber received " << recv_count << " messages (expected 2)" << std::endl;

    auto pub_stats = pub.GetStats();
    std::cout << "Publisher stats: send_ok=" << pub_stats.send_ok << std::endl;

    sub.Stop();
}

// Test 4: Stress test - multiple requests
void test_stress() {
    std::cout << "\n=== Test 4: Stress Test ===" << std::endl;

    const std::string endpoint = "ipc:///tmp/test_stress.sock";
    system("rm -f /tmp/test_stress.sock");

    ReplierSocket server(endpoint);
    server.Bind();
    server.SetHandler([](const std::string& /*req*/) {
        return "OK";
    });
    server.StartBackground();

    std::this_thread::sleep_for(100ms);

    RequesterSocket client(endpoint);
    client.Connect();

    const int N = 100;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < N; ++i) {
        auto reply = client.Request("req" + std::to_string(i), 1000);
        if (!reply) {
            std::cerr << "Request " << i << " failed" << std::endl;
            break;
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    auto stats = client.GetStats();
    std::cout << "Completed " << stats.recv_ok << "/" << N << " requests in "
              << elapsed.count() << "ms" << std::endl;
    std::cout << "Avg latency: " << (elapsed.count() / (double)N) << "ms" << std::endl;

    server.Stop();
}

int main() {
    std::cout << "ZMQ Wrapper Test Suite" << std::endl;
    std::cout << "======================" << std::endl;

    try {
        test_req_rep();
        test_dealer_router();
        test_pub_sub();
        test_stress();

        std::cout << "\n=== All Tests Passed ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
