// Minimal logger stub for standalone test builds
#pragma once
#include <iostream>
#include <sstream>

namespace zmq_test_logger {

struct LogLine {
    std::ostream& out;
    bool newline;
    explicit LogLine(std::ostream& o) : out(o), newline(true) {}
    ~LogLine() { if (newline) out << '\n'; }
    template<typename T>
    LogLine& operator<<(const T& v) { out << v; return *this; }
};

}  // namespace zmq_test_logger

#define log_debug zmq_test_logger::LogLine(std::cout)
#define log_info  zmq_test_logger::LogLine(std::cout)
#define log_warn  zmq_test_logger::LogLine(std::cout) << "[WARN] "
#define log_error zmq_test_logger::LogLine(std::cerr) << "[ERR] "
