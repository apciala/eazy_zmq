#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <json/json.h>
#include <zmq.hpp>

#include "reqrep_client.h"
#include "sub_client.h"

namespace {

enum class Mode {
    REQ,
    SUB,
    BOTH,
    GATEWAY,
    STRESS,
};

struct CliOptions {
    std::string component;
    Mode mode = Mode::REQ;
    std::string cmd;
    std::string telemetry;
    std::string params_text = "{}";
    std::string req_rep_endpoint;
    std::string pub_sub_endpoint;
    std::string sub_topic = "v1/notify/";
    std::string type;
    std::string msg_id;
    std::string client_name = "clientA";  // Gateway 模式用
    std::string target_name = "clientB";  // Gateway 模式用
    int stress_rate  = 100;   // 压测：每秒发送条数，0=全速
    int stress_count = 0;     // 压测：总发送条数，0=无限
    int timeout_ms = 3000;
    int sub_timeout_ms = 1000;
    int sub_count = 0;
    bool verbose = false;
};

void PrintUsage(const char* prog) {
    std::cout
        << "Usage:\n"
        << "  " << prog << " --component <name> [--mode req|sub|both|gateway] [options]\n\n"
        << "Options:\n"
        << "  --component <name>           Component name (required)\n"
        << "  --mode <req|sub|both|gateway> Run mode (default: req)\n"
        << "  --cmd <name>                 Command name for req mode\n"
        << "  --telemetry <name>           Telemetry name for req mode\n"
        << "  --type <cmd|telemetry>       Explicit message type (optional)\n"
        << "  --params <json>              JSON params string (default: {})\n"
        << "  --req-rep-endpoint <ipc://>  Override req-rep endpoint\n"
        << "  --pub-sub-endpoint <ipc://>  Override pub-sub endpoint\n"
        << "  --sub-topic <prefix>         Subscriber topic (default: v1/notify/)\n"
        << "  --timeout-ms <ms>            REQ/REP timeout (default: 3000)\n"
        << "  --sub-timeout-ms <ms>        SUB poll timeout (default: 1000)\n"
        << "  --sub-count <n>              Exit after n messages (0 means infinite)\n"
        << "  --msg-id <id>                Override msgId\n"
        << "  --client-name <name>         Client name for gateway/stress mode (default: clientA)\n"
        << "  --target-name <name>         Target name for gateway/stress mode (default: clientB)\n"
        << "  --stress-rate <n>            Stress: messages per second, 0=unlimited (default: 100)\n"
        << "  --stress-count <n>           Stress: total messages to send, 0=infinite (default: 0)\n"
        << "  --verbose                    Enable verbose logs\n"
        << "  -h, --help                   Show this help\n"
        << "\nGateway mode:\n"
        << "  Tests zmq_gateway with REP/SUB/ROUTER channels\n";
}

std::string BuildDefaultReqRepEndpoint(const std::string& component) {
    return "ipc:///userdata/ipc/" + component + "/reqrep.sock";
}

std::string BuildDefaultPubSubEndpoint(const std::string& component) {
    return "ipc:///userdata/ipc/" + component + "/pubsub.sock";
}

std::string GenerateMsgId() {
    static std::mt19937_64 rng(std::random_device{}());
    std::ostringstream os;
    os << "cli-" << std::hex << rng();
    return os.str();
}

long long CurrentMsTs() {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
    return static_cast<long long>(now.time_since_epoch().count());
}

bool ParseArgs(int argc, char** argv, CliOptions& opts, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto need_value = [&](const std::string& name) -> const char* {
            if (i + 1 >= argc) {
                err = "Missing value for " + name;
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--component") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.component = v;
        } else if (arg == "--mode") {
            const char* v = need_value(arg);
            if (!v) return false;
            std::string mode = v;
            if (mode == "req") opts.mode = Mode::REQ;
            else if (mode == "sub") opts.mode = Mode::SUB;
            else if (mode == "both") opts.mode = Mode::BOTH;
            else if (mode == "gateway") opts.mode = Mode::GATEWAY;
            else if (mode == "stress") opts.mode = Mode::STRESS;
            else {
                err = "Invalid mode: " + mode;
                return false;
            }
        } else if (arg == "--cmd") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.cmd = v;
        } else if (arg == "--telemetry") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.telemetry = v;
        } else if (arg == "--type") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.type = v;
        } else if (arg == "--params") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.params_text = v;
        } else if (arg == "--req-rep-endpoint") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.req_rep_endpoint = v;
        } else if (arg == "--pub-sub-endpoint") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.pub_sub_endpoint = v;
        } else if (arg == "--sub-topic") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.sub_topic = v;
        } else if (arg == "--timeout-ms") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.timeout_ms = std::stoi(v);
        } else if (arg == "--sub-timeout-ms") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.sub_timeout_ms = std::stoi(v);
        } else if (arg == "--sub-count") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.sub_count = std::stoi(v);
        } else if (arg == "--msg-id") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.msg_id = v;
        } else if (arg == "--client-name") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.client_name = v;
        } else if (arg == "--target-name") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.target_name = v;
        } else if (arg == "--stress-rate") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.stress_rate = std::stoi(v);
        } else if (arg == "--stress-count") {
            const char* v = need_value(arg);
            if (!v) return false;
            opts.stress_count = std::stoi(v);
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            std::exit(0);
        } else {
            err = "Unknown argument: " + arg;
            return false;
        }
    }

    if (opts.component.empty()) {
        err = "--component is required";
        return false;
    }

    if (opts.req_rep_endpoint.empty()) {
        opts.req_rep_endpoint = BuildDefaultReqRepEndpoint(opts.component);
    }
    if (opts.pub_sub_endpoint.empty()) {
        opts.pub_sub_endpoint = BuildDefaultPubSubEndpoint(opts.component);
    }
    if (opts.msg_id.empty()) {
        opts.msg_id = GenerateMsgId();
    }

    return true;
}

bool BuildRequest(const CliOptions& opts, Json::Value& request, std::string& err) {
    Json::CharReaderBuilder rb;
    std::string parse_errors;
    std::istringstream is(opts.params_text);
    Json::Value params;
    if (!Json::parseFromStream(rb, is, &params, &parse_errors)) {
        err = "Invalid --params JSON: " + parse_errors;
        return false;
    }

    std::string type = opts.type;
    if (type.empty()) {
        type = opts.telemetry.empty() ? "cmd" : "telemetry";
    }

    if (type != "cmd" && type != "telemetry") {
        err = "--type must be cmd or telemetry";
        return false;
    }

    request["componentName"] = opts.component;
    request["msgId"] = opts.msg_id;
    request["type"] = type;
    request["ts"] = static_cast<Json::Int64>(CurrentMsTs());
    request["params"] = params;

    if (type == "cmd") {
        if (opts.cmd.empty()) {
            err = "--cmd is required for cmd message";
            return false;
        }
        request["cmd"] = opts.cmd;
    } else {
        if (opts.telemetry.empty()) {
            err = "--telemetry is required for telemetry message";
            return false;
        }
        request["telemetry"] = opts.telemetry;
    }

    return true;
}

int RunReq(const CliOptions& opts) {
    Json::Value request;
    std::string err;
    if (!BuildRequest(opts, request, err)) {
        std::cerr << "Error: " << err << std::endl;
        return 2;
    }

    ReqRepOptions rr;
    rr.endpoint = opts.req_rep_endpoint;
    rr.timeout_ms = opts.timeout_ms;
    rr.verbose = opts.verbose;

    std::string response;
    if (!SendAndReceiveJson(rr, request, response, err)) {
        std::cerr << "REQ/REP failed: " << err << std::endl;
        return 3;
    }

    std::cout << response << std::endl;
    return 0;
}

int RunSub(const CliOptions& opts) {
    SubOptions so;
    so.endpoint = opts.pub_sub_endpoint;
    so.topic = opts.sub_topic;
    so.timeout_ms = opts.sub_timeout_ms;
    so.count = opts.sub_count;
    so.verbose = opts.verbose;
    return RunSubscriber(so);
}

int RunGateway(const CliOptions& opts) {
    std::cout << "\n=== Testing zmq_gateway (client=" << opts.client_name << ") ===\n";

    zmq::context_t ctx(1);

    // Test 1: REP command channel
    std::cout << "\n--- Test 1: REP Command ---\n";
    zmq::socket_t req(ctx, zmq::socket_type::req);
    req.connect("ipc:///userdata/ipc/zmq_gateway/cmd.sock");
    req.set(zmq::sockopt::rcvtimeo, opts.timeout_ms);

    Json::Value cmd;
    cmd["cmd"] = "register";
    cmd["name"] = opts.client_name;
    cmd["identity"] = opts.client_name;  // 必须和 DEALER routing_id 一致

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
        std::cout << "No reply (timeout)\n";
    }

    // Test 2: ROUTER 4-frame protocol，后台定时发送 + 前台持续接收
    std::cout << "\n--- Test 2: ROUTER 4-Frame (Ctrl+C to stop) ---\n";
    zmq::socket_t dealer(ctx, zmq::socket_type::dealer);
    dealer.set(zmq::sockopt::routing_id, opts.client_name);
    dealer.connect("ipc:///userdata/ipc/zmq_gateway/router.sock");
    dealer.set(zmq::sockopt::rcvtimeo, 500);

    std::atomic<bool> running{true};
    int send_seq = 0;

    // 后台线程：每 2 秒发一次
    std::thread sender([&]() {
        while (running) {
            std::string data = "Hello from " + opts.client_name
                               + " seq=" + std::to_string(++send_seq);
            dealer.send(zmq::buffer(opts.target_name), zmq::send_flags::sndmore);
            dealer.send(zmq::buffer(std::string{}),    zmq::send_flags::sndmore);
            dealer.send(zmq::buffer(data),             zmq::send_flags::none);
            std::cout << "[SEND] -> " << opts.target_name << " : " << data << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });

    // 主线程：持续接收
    std::cout << "Running... press Ctrl+C to stop\n";
    while (true) {
        zmq::message_t f1, f2, f3;
        auto r1 = dealer.recv(f1, zmq::recv_flags::none);
        if (!r1) continue;  // timeout，继续等

        dealer.recv(f2, zmq::recv_flags::none);
        dealer.recv(f3, zmq::recv_flags::none);
        std::string src(static_cast<char*>(f1.data()), f1.size());
        std::string msg(static_cast<char*>(f3.data()), f3.size());
        std::cout << "[RECV] <- " << src << " : " << msg << "\n";
    }

    running = false;
    sender.join();
    return 0;
}

int RunStress(const CliOptions& opts) {
    using Clock = std::chrono::steady_clock;

    std::cout << "\n=== Stress Test ==="
              << " client=" << opts.client_name
              << " target=" << opts.target_name
              << " rate=" << (opts.stress_rate == 0 ? "unlimited" : std::to_string(opts.stress_rate) + "/s")
              << " count=" << (opts.stress_count == 0 ? "infinite" : std::to_string(opts.stress_count))
              << "\nPress Ctrl+C to stop\n\n";

    zmq::context_t ctx(1);

    // 注册到 Gateway
    zmq::socket_t req(ctx, zmq::socket_type::req);
    req.connect("ipc:///userdata/ipc/zmq_gateway/cmd.sock");
    req.set(zmq::sockopt::rcvtimeo, opts.timeout_ms);

    Json::Value cmd;
    cmd["cmd"] = "register";
    cmd["name"] = opts.client_name;
    cmd["identity"] = opts.client_name;
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    req.send(zmq::buffer(Json::writeString(writer, cmd)), zmq::send_flags::none);
    zmq::message_t reg_reply;
    if (req.recv(reg_reply)) {
        std::cout << "[REG] " << std::string(static_cast<char*>(reg_reply.data()), reg_reply.size()) << "\n\n";
    } else {
        std::cerr << "[REG] Failed to register, gateway not running?\n";
        return 1;
    }

    // DEALER socket — 单线程收发，ZMQ socket 不能跨线程使用
    zmq::socket_t dealer(ctx, zmq::socket_type::dealer);
    dealer.set(zmq::sockopt::routing_id, opts.client_name);
    dealer.set(zmq::sockopt::sndhwm, 10000);
    dealer.set(zmq::sockopt::rcvhwm, 10000);
    dealer.connect("ipc:///userdata/ipc/zmq_gateway/router.sock");

    uint64_t sent    = 0;
    uint64_t recv_ok = 0;
    uint64_t seq     = 0;

    using Clock = std::chrono::steady_clock;
    const auto interval = opts.stress_rate > 0
        ? std::chrono::microseconds(1000000 / opts.stress_rate)
        : std::chrono::microseconds(0);

    auto next_send  = Clock::now();
    auto next_stats = Clock::now() + std::chrono::seconds(1);
    auto start      = Clock::now();
    uint64_t last_sent = 0, last_recv = 0;

    zmq::pollitem_t items[] = {{dealer.handle(), 0, ZMQ_POLLIN, 0}};

    std::cout << "Running... press Ctrl+C to stop\n\n";

    while (true) {
        auto now = Clock::now();

        // 发送
        bool can_send = (opts.stress_count == 0 || sent < static_cast<uint64_t>(opts.stress_count));
        if (can_send && now >= next_send) {
            ++seq;
            std::string data = "stress-" + std::to_string(seq);
            dealer.send(zmq::buffer(opts.target_name), zmq::send_flags::sndmore);
            dealer.send(zmq::buffer(std::string{}),    zmq::send_flags::sndmore);
            dealer.send(zmq::buffer(data),             zmq::send_flags::none);
            ++sent;
            if (interval.count() > 0) next_send = now + interval;
        }

        // poll 等待接收（最多 1ms，避免阻塞发送）
        zmq::poll(items, 1, std::chrono::milliseconds(1));
        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t f1, f2, f3;
            (void)dealer.recv(f1, zmq::recv_flags::none);
            (void)dealer.recv(f2, zmq::recv_flags::none);
            (void)dealer.recv(f3, zmq::recv_flags::none);
            ++recv_ok;
        }

        // 每秒统计
        now = Clock::now();
        if (now >= next_stats) {
            double elapsed = std::chrono::duration<double>(now - start).count();
            std::cout << "[STATS]"
                      << " elapsed=" << static_cast<int>(elapsed) << "s"
                      << "  send=" << sent    << "(" << (sent    - last_sent) << "/s)"
                      << "  recv=" << recv_ok << "(" << (recv_ok - last_recv) << "/s)"
                      << "  loss=" << (sent > recv_ok ? sent - recv_ok : 0)
                      << "\n";
            last_sent  = sent;
            last_recv  = recv_ok;
            next_stats = now + std::chrono::seconds(1);
        }

        // 发完后等剩余消息到达
        if (opts.stress_count > 0 && sent >= static_cast<uint64_t>(opts.stress_count)) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << "\n[DONE] sent=" << sent << " recv=" << recv_ok
                      << " loss=" << (sent - recv_ok) << "\n";
            break;
        }
    }

    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    CliOptions opts;
    std::string err;
    if (!ParseArgs(argc, argv, opts, err)) {
        std::cerr << "Error: " << err << "\n\n";
        PrintUsage(argv[0]);
        return 1;
    }

    int rc = 0;
    if (opts.mode == Mode::REQ) {
        rc = RunReq(opts);
    } else if (opts.mode == Mode::SUB) {
        rc = RunSub(opts);
    } else if (opts.mode == Mode::GATEWAY) {
        rc = RunGateway(opts);
    } else if (opts.mode == Mode::STRESS) {
        rc = RunStress(opts);
    } else {
        rc = RunReq(opts);
        if (rc == 0) {
            rc = RunSub(opts);
        }
    }
    return rc;
}
