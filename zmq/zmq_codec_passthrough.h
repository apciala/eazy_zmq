#pragma once

#include "i_zmq_codec.h"

// 纯字符串透传 codec：不做任何序列化，payload 原样收发。
// 适用于调用方自己管理序列化格式（JSON/protobuf/自定义协议）的场景。
class ZmqPassthroughCodec : public IZmqCodec {
public:
    bool Encode(const TransportMessage& message, std::string& out) override {
        out = message.payload;
        return true;
    }

    bool Decode(const std::string& in, TransportMessage& message) override {
        message.payload = in;
        return true;
    }
};
