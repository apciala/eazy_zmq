#pragma once

#include <string>

class cconfig;

// 通用 ZMQ socket 参数，各通信类（ZmqRepServer/ZmqReqClient/ZmqPublisher/ZmqSubscriber）共用。
// 不包含 mode/bind_or_connect，各类自己知道自己的模式。
struct ZmqSocketOptions {
    int send_timeout_ms = 3000;
    int recv_timeout_ms = 3000;
    int reconnect_ms    = 1000;
    int snd_hwm         = 1000;
    int rcv_hwm         = 1000;
    int linger_ms       = 0;
};

bool LoadZmqOptionsFromSelfSettings(cconfig& cfg, ZmqSocketOptions& out_opts);
bool LoadZmqOptionsForCurrentDevice(ZmqSocketOptions& out_opts);
