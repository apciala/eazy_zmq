#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "config.h"
#include "gateway.h"
#include "logger.h"

namespace {
std::atomic<bool> g_running{true};

void SignalHandler(int /*signal*/) {
    g_running = false;
}

void PrintUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  -c, --config <file>  Load config from file\n"
              << "  -h, --help           Show this help message\n"
              << "\n"
              << "Environment variables:\n"
              << "  ZMQ_GATEWAY_REP_ENDPOINT     REP socket endpoint\n"
              << "  ZMQ_GATEWAY_ROUTER_ENDPOINT  ROUTER socket endpoint\n"
              << "  ZMQ_GATEWAY_PUB_ENDPOINT    PUB socket endpoint\n"
              << "\n"
              << "Default endpoints:\n"
              << "  REP:    ipc:///userdata/ipc/zmq_gateway/cmd.sock\n"
              << "  ROUTER: ipc:///userdata/ipc/zmq_gateway/router.sock\n"
              << "  PUB:    ipc:///userdata/ipc/zmq_gateway/pub.sock\n"
              << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
    // 解析命令行参数
    std::string config_file;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            PrintUsage(argv[0]);
            return 1;
        }
    }

    // 设置信号处理
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    // 加载配置
    zmq_gateway::GatewayConfig config;

    // 先从文件加载
    if (!config_file.empty()) {
        config.LoadFromFile(config_file);
    }

    // 环境变量覆盖
    config.LoadFromEnv();

    // 创建并启动网关
    zmq_gateway::Gateway gateway(config);

    if (!gateway.Start()) {
        log_error << "[Main] Failed to start gateway";
        return 1;
    }

    log_info << "[Main] ZMQ Gateway started";
    log_info << "[Main] Press Ctrl+C to stop";

    // 主循环
    while (g_running && gateway.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 停止网关
    log_info << "[Main] Shutting down...";
    gateway.Stop();

    log_info << "[Main] Bye";
    return 0;
}