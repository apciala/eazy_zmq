#pragma once

#include "i_zmq_codec.h"

class ZmqJsonCodec : public IZmqCodec {
public:
    bool Encode(const TransportMessage& message, std::string& out) override;
    bool Decode(const std::string& in, TransportMessage& message) override;
};
