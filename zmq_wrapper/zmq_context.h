#pragma once

#include <zmq.hpp>

namespace zmq_wrap {

/// 进程级 ZMQ context 全局单例。
///
/// 用户代码不需要直接使用此类，各 Socket 类内部自动获取。
class ZmqGlobalCtx {
public:
    /// 获取全局 zmq::context_t 引用。
    static zmq::context_t& get();

    ZmqGlobalCtx(const ZmqGlobalCtx&)            = delete;
    ZmqGlobalCtx& operator=(const ZmqGlobalCtx&) = delete;

private:
    ZmqGlobalCtx();
    zmq::context_t ctx_;
};

}  // namespace zmq_wrap
