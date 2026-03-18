#pragma once

#include <string>

// TransportMessage: ZMQ 传输层消息载体。
// - topic:   PUB/SUB 场景下的消息主题（multipart 第一帧）；REQ/REP 场景留空。
// - payload: 消息内容，格式由上层决定（纯字符串、JSON、protobuf 等）。
struct TransportMessage {
    std::string topic;
    std::string payload;
};

class IZmqCodec {
public:
    virtual ~IZmqCodec() = default;

    virtual bool Encode(const TransportMessage& message, std::string& out) = 0;
    virtual bool Decode(const std::string& in, TransportMessage& message) = 0;
};
