#include "config.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "logger.h"

namespace zmq_gateway {

bool GatewayConfig::LoadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        log_warn << "[Gateway] Config file not found: " << path << ", using defaults";
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') continue;

        // 解析 key=value
        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // 去除首尾空白
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t'))
            key.erase(0, 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
            key.pop_back();
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
            value.erase(0, 1);
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' || value.back() == '\n'))
            value.pop_back();

        if (key == "rep_endpoint") {
            rep_endpoint = value;
        } else if (key == "router_endpoint") {
            router_endpoint = value;
        } else if (key == "pub_endpoint") {
            pub_endpoint = value;
        }
    }

    log_info << "[Gateway] Config loaded from: " << path;
    return true;
}

void GatewayConfig::LoadFromEnv() {
    const char* env = nullptr;

    env = std::getenv("ZMQ_GATEWAY_REP_ENDPOINT");
    if (env) rep_endpoint = env;

    env = std::getenv("ZMQ_GATEWAY_ROUTER_ENDPOINT");
    if (env) router_endpoint = env;

    env = std::getenv("ZMQ_GATEWAY_PUB_ENDPOINT");
    if (env) pub_endpoint = env;
}

}  // namespace zmq_gateway