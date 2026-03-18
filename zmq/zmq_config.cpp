#include "zmq_config.h"

#include "../../applicaiton/app_common/app_common.h"
#include "../../applicaiton/config/config_dir.h"
#include "../cconfig/cconfig.h"

namespace {

int ReadInt(cconfig& cfg, const std::string& section, const std::string& key, int default_value)
{
    int value = default_value;
    cfg.read_config(section, key, value);
    return value;
}

} // namespace

bool LoadZmqOptionsFromSelfSettings(cconfig& cfg, ZmqSocketOptions& out_opts)
{
    out_opts.send_timeout_ms = ReadInt(cfg, "zmq", "send_timeout_ms", out_opts.send_timeout_ms);
    out_opts.recv_timeout_ms = ReadInt(cfg, "zmq", "recv_timeout_ms", out_opts.recv_timeout_ms);
    out_opts.reconnect_ms    = ReadInt(cfg, "zmq", "reconnect_ms",    out_opts.reconnect_ms);
    out_opts.snd_hwm         = ReadInt(cfg, "zmq", "snd_hwm",         out_opts.snd_hwm);
    out_opts.rcv_hwm         = ReadInt(cfg, "zmq", "rcv_hwm",         out_opts.rcv_hwm);
    out_opts.linger_ms       = ReadInt(cfg, "zmq", "linger_ms",       out_opts.linger_ms);
    return true;
}

bool LoadZmqOptionsForCurrentDevice(ZmqSocketOptions& out_opts)
{
    const std::string cfg_path = get_maindevice_path() + CFG_FILE_SELF_SETTING;
    cconfig cfg(cfg_path);
    if (!cfg.is_valid()) {
        log_error << "Failed to open self setting config for ZMQ: " << cfg_path.c_str();
        return false;
    }
    return LoadZmqOptionsFromSelfSettings(cfg, out_opts);
}
