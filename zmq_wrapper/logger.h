#pragma once

// 简单的日志兼容层，用于独立编译
// 原项目使用 log_debug / log_info / log_warn / log_error 宏

#include <iostream>
#include <sstream>

namespace zmq_log_detail {

class LogStream {
public:
    explicit LogStream(const char* level) : level_(level) {}
    ~LogStream() { std::cout << level_ << " " << ss_.str() << std::endl; }
    
    std::stringstream& stream() { return ss_; }
    
private:
    const char* level_;
    std::stringstream ss_;
};

}  // namespace zmq_log_detail

#define log_debug zmq_log_detail::LogStream("[DEBUG]").stream()
#define log_info zmq_log_detail::LogStream("[INFO]").stream()
#define log_warn zmq_log_detail::LogStream("[WARN]").stream()
#define log_error zmq_log_detail::LogStream("[ERROR]").stream()
