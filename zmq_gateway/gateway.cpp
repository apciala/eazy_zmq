#include "gateway.h"

#include <chrono>
#include <thread>

#include "logger.h"
#include "zmq_wrapper.h"

namespace zmq_gateway {

namespace {

// 简单 JSON 解析（避免引入 nlohmann/json 依赖）
// 提取 JSON 字符串中的字符串值
std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";

    // 找到冒号
    pos = json.find(':', pos + search.length());
    if (pos == std::string::npos) return "";

    // 跳过空白
    pos++;
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        pos++;
    }

    // 找到引号
    if (pos >= json.length() || json[pos] != '"') return "";
    pos++;

    // 找到结束引号
    size_t end = pos;
    while (end < json.length() && json[end] != '"') {
        if (json[end] == '\\' && end + 1 < json.length()) {
            end += 2;  // 跳过转义字符
        } else {
            end++;
        }
    }

    return json.substr(pos, end - pos);
}

// 简单 JSON 构建
std::string BuildJsonResponse(bool success, const std::string& message, const std::string& data = "") {
    std::string result = "{\"success\":";
    result += success ? "true" : "false";
    result += ",\"message\":\"";
    result += message;
    result += "\"";
    if (!data.empty()) {
        result += ",";
        result += data;
    }
    result += "}";
    return result;
}

}  // namespace

Gateway::Gateway(const GatewayConfig& config)
    : m_config(config)
{
}

Gateway::~Gateway() noexcept
{
    Stop();
}

bool Gateway::Start()
{
    if (m_running) {
        log_warn << "[Gateway] Already running";
        return true;
    }

    log_info << "[Gateway] Starting...";
    log_info << "[Gateway] REP endpoint: " << m_config.rep_endpoint;
    log_info << "[Gateway] ROUTER endpoint: " << m_config.router_endpoint;
    log_info << "[Gateway] PUB endpoint: " << m_config.pub_endpoint;

    // 创建 sockets
    m_rep = std::make_unique<zmq_wrap::ReplierSocket>(m_config.rep_endpoint);
    m_router = std::make_unique<zmq_wrap::RouterSocket>(m_config.router_endpoint);
    m_pub = std::make_unique<zmq_wrap::PublisherSocket>(m_config.pub_endpoint);

    // 绑定 REP socket
    if (!m_rep->Bind()) {
        log_error << "[Gateway] Failed to bind REP socket";
        return false;
    }

    // 绑定 ROUTER socket
    if (!m_router->Bind()) {
        log_error << "[Gateway] Failed to bind ROUTER socket";
        m_rep.reset();
        return false;
    }

    // 绑定 PUB socket
    if (!m_pub->Bind()) {
        log_error << "[Gateway] Failed to bind PUB socket";
        m_router.reset();
        m_rep.reset();
        return false;
    }

    // 设置 REP 请求处理器
    m_rep->SetHandler([this](const std::string& payload) {
        return HandleRepRequest(payload);
    });

    // 启动 REP 后台服务
    if (!m_rep->StartBackground()) {
        log_error << "[Gateway] Failed to start REP background thread";
        m_pub.reset();
        m_router.reset();
        m_rep.reset();
        return false;
    }

    // 启动 ROUTER 后台服务
    if (!m_router->StartBackground([this](const zmq_wrap::RoutedFrames& msg) {
        HandleRouterMessage(msg);
    })) {
        log_error << "[Gateway] Failed to start ROUTER background thread";
        m_rep->Stop();
        m_pub.reset();
        m_router.reset();
        m_rep.reset();
        return false;
    }

    m_running = true;
    log_info << "[Gateway] Started successfully";
    return true;
}

void Gateway::Stop() noexcept
{
    if (!m_running) return;

    log_info << "[Gateway] Stopping...";
    m_running = false;

    if (m_rep) {
        m_rep->Stop();
    }
    if (m_router) {
        m_router->Stop();
    }

    m_pub.reset();
    m_router.reset();
    m_rep.reset();

    log_info << "[Gateway] Stopped";
}

size_t Gateway::GetClientCount() const
{
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    return m_clients.size();
}

Gateway::Stats Gateway::GetStats() const
{
    std::lock_guard<std::mutex> lock(m_stats_mutex);
    return m_stats;
}

std::string Gateway::HandleRepRequest(const std::string& payload)
{
    log_debug << "[Gateway] REP received: " << payload;

    // 解析命令
    std::string cmd = ExtractJsonString(payload, "cmd");

    if (cmd == "register") {
        std::string name = ExtractJsonString(payload, "name");
        std::string identity = ExtractJsonString(payload, "identity");

        if (name.empty()) {
            return BuildJsonResponse(false, "missing 'name' field");
        }
        if (identity.empty()) {
            return BuildJsonResponse(false, "missing 'identity' field");
        }

        if (RegisterClient(name, identity)) {
            log_info << "[Gateway] Client registered: name=" << name << " identity=" << identity;

            // 广播注册通知
            Broadcast("register", "{\"event\":\"register\",\"name\":\"" + name + "\"}");

            return BuildJsonResponse(true, "registered successfully");
        } else {
            return BuildJsonResponse(true, "already registered (updated)");
        }
    } else if (cmd == "unregister") {
        std::string name = ExtractJsonString(payload, "name");

        if (name.empty()) {
            return BuildJsonResponse(false, "missing 'name' field");
        }

        {
            std::lock_guard<std::mutex> lock(m_clients_mutex);
            m_clients.erase(name);
        }

        log_info << "[Gateway] Client unregistered: name=" << name;
        Broadcast("unregister", "{\"event\":\"unregister\",\"name\":\"" + name + "\"}");

        return BuildJsonResponse(true, "unregistered successfully");
    } else if (cmd == "list") {
        // 列出所有已注册客户端
        std::string clients_list = "\"clients\":[";
        {
            std::lock_guard<std::mutex> lock(m_clients_mutex);
            bool first = true;
            for (const auto& [name, identity] : m_clients) {
                if (!first) clients_list += ",";
                clients_list += "\"" + name + "\"";
                first = false;
            }
        }
        clients_list += "]";

        return BuildJsonResponse(true, "ok", clients_list);
    } else if (cmd == "stats") {
        // 返回统计信息
        Stats s = GetStats();
        std::string data = "\"stats\":{";
        data += "\"registrations\":" + std::to_string(s.registrations);
        data += ",\"messages_routed\":" + std::to_string(s.messages_routed);
        data += ",\"broadcasts\":" + std::to_string(s.broadcasts);
        data += ",\"errors\":" + std::to_string(s.errors);
        data += "}";

        return BuildJsonResponse(true, "ok", data);
    } else if (cmd.empty()) {
        return BuildJsonResponse(false, "missing 'cmd' field");
    } else {
        return BuildJsonResponse(false, "unknown command: " + cmd);
    }
}

void Gateway::HandleRouterMessage(const zmq_wrap::RoutedFrames& msg)
{
    // ROUTER 接收格式：[identity][payload]
    // msg.peer_id = identity, msg.frames = payload frames

    if (msg.frames.empty()) {
        log_warn << "[Gateway] ROUTER received empty frames from " << msg.peer_id;
        return;
    }

    // 假设第一帧是 payload（JSON）
    const std::string& payload = msg.frames[0];
    log_debug << "[Gateway] ROUTER received from " << msg.peer_id << ": " << payload;

    // 解析目标客户端
    std::string target = ExtractJsonString(payload, "target");

    if (target.empty()) {
        log_warn << "[Gateway] Message missing 'target' field";
        {
            std::lock_guard<std::mutex> lock(m_stats_mutex);
            ++m_stats.errors;
        }
        return;
    }

    // 查找目标客户端 identity
    std::string target_identity = FindClientIdentity(target);

    if (target_identity.empty()) {
        log_warn << "[Gateway] Target client not found: " << target;
        {
            std::lock_guard<std::mutex> lock(m_stats_mutex);
            ++m_stats.errors;
        }
        return;
    }

    // 转发消息
    if (m_router->SendTo(target_identity, payload)) {
        log_debug << "[Gateway] Routed message to " << target << " (identity=" << target_identity << ")";
        {
            std::lock_guard<std::mutex> lock(m_stats_mutex);
            ++m_stats.messages_routed;
        }
    } else {
        log_error << "[Gateway] Failed to route message to " << target;
        {
            std::lock_guard<std::mutex> lock(m_stats_mutex);
            ++m_stats.errors;
        }
    }
}

bool Gateway::RegisterClient(const std::string& name, const std::string& identity)
{
    std::lock_guard<std::mutex> lock(m_clients_mutex);

    bool is_new = (m_clients.find(name) == m_clients.end());
    m_clients[name] = identity;

    {
        std::lock_guard<std::mutex> stats_lock(m_stats_mutex);
        ++m_stats.registrations;
    }

    return is_new;
}

std::string Gateway::FindClientIdentity(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(m_clients_mutex);
    auto it = m_clients.find(name);
    if (it != m_clients.end()) {
        return it->second;
    }
    return "";
}

void Gateway::Broadcast(const std::string& topic, const std::string& message)
{
    if (m_pub) {
        m_pub->Publish(topic, message);
        {
            std::lock_guard<std::mutex> lock(m_stats_mutex);
            ++m_stats.broadcasts;
        }
    }
}

}  // namespace zmq_gateway