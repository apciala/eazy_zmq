#include "zmq_codec_json.h"

#include <memory>
#include <jsoncpp/json/json.h>

namespace {

Json::StreamWriterBuilder MakeCompactWriter()
{
    Json::StreamWriterBuilder w;
    w["indentation"] = "";
    return w;
}

} // namespace

bool ZmqJsonCodec::Encode(const TransportMessage& message, std::string& out)
{
    if (message.payload.empty()) {
        out = "{}";
        return true;
    }

    // 如果 payload 本身是合法 JSON，紧凑序列化后直接发出
    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    if (reader->parse(message.payload.data(),
                      message.payload.data() + message.payload.size(),
                      &root, &errs)) {
        out = Json::writeString(MakeCompactWriter(), root);
        return true;
    }

    // payload 不是 JSON，包成 {"data":"..."} 保证对端收到合法 JSON
    Json::Value wrapper;
    wrapper["data"] = message.payload;
    out = Json::writeString(MakeCompactWriter(), wrapper);
    return true;
}

bool ZmqJsonCodec::Decode(const std::string& in, TransportMessage& message)
{
    if (in.empty()) {
        message.payload = "{}";
        return true;
    }

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    std::unique_ptr<Json::CharReader> reader(rb.newCharReader());
    if (!reader->parse(in.data(), in.data() + in.size(), &root, &errs)) {
        // 收到非 JSON 内容，原样放入 payload，不丢弃
        message.payload = in;
        return true;
    }

    message.payload = Json::writeString(MakeCompactWriter(), root);
    return true;
}
