#include "zmq_context.h"

#include "logger.h"

namespace zmq_wrap {

ZmqGlobalCtx::ZmqGlobalCtx()
    : ctx_(1)  // 1 个 I/O 线程
{
    log_info << "[ZMQ] context initialized";
}

zmq::context_t& ZmqGlobalCtx::get()
{
    // 函数级静态：C++11 保证线程安全初始化
    static ZmqGlobalCtx s_instance;
    return s_instance.ctx_;
}

}  // namespace zmq_wrap
