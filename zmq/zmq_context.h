#pragma once

#include <zmq.hpp>

// 全局 ZMQ context 单例。整个进程共享一个 context，ZMQ 官方推荐做法。
// 用法：ZmqContext::Get()
class ZmqContext {
public:
    ZmqContext(const ZmqContext&) = delete;
    ZmqContext& operator=(const ZmqContext&) = delete;

    static zmq::context_t& Get() {
        static zmq::context_t s_ctx(1);
        return s_ctx;
    }

private:
    ZmqContext() = default;
};
