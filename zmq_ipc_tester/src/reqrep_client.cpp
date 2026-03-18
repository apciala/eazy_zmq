#include "reqrep_client.h"

#include <iostream>
#include <memory>
#include <string>

#include <zmq.h>

namespace {

std::string ToJsonString(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

}  // namespace

bool SendAndReceiveJson(const ReqRepOptions& options,
                        const Json::Value& request,
                        std::string& response,
                        std::string& error) {
    void* ctx = zmq_ctx_new();
    if (!ctx) {
        error = "zmq_ctx_new failed";
        return false;
    }

    void* sock = zmq_socket(ctx, ZMQ_REQ);
    if (!sock) {
        error = "zmq_socket(ZMQ_REQ) failed";
        zmq_ctx_term(ctx);
        return false;
    }

    zmq_setsockopt(sock, ZMQ_RCVTIMEO, &options.timeout_ms, sizeof(options.timeout_ms));
    zmq_setsockopt(sock, ZMQ_SNDTIMEO, &options.timeout_ms, sizeof(options.timeout_ms));

    if (options.verbose) {
        std::cerr << "[REQ] connect " << options.endpoint << std::endl;
    }

    if (zmq_connect(sock, options.endpoint.c_str()) != 0) {
        error = "zmq_connect failed: " + std::to_string(zmq_errno());
        zmq_close(sock);
        zmq_ctx_term(ctx);
        return false;
    }

    const std::string request_text = ToJsonString(request);
    if (options.verbose) {
        std::cerr << "[REQ] send " << request_text << std::endl;
    }

    const int send_rc = zmq_send(sock, request_text.data(), request_text.size(), 0);
    if (send_rc < 0) {
        error = "zmq_send failed: " + std::to_string(zmq_errno());
        zmq_close(sock);
        zmq_ctx_term(ctx);
        return false;
    }

    zmq_msg_t msg;
    zmq_msg_init(&msg);
    const int recv_rc = zmq_msg_recv(&msg, sock, 0);
    if (recv_rc < 0) {
        error = "zmq_msg_recv failed: " + std::to_string(zmq_errno());
        zmq_msg_close(&msg);
        zmq_close(sock);
        zmq_ctx_term(ctx);
        return false;
    }

    response.assign(static_cast<const char*>(zmq_msg_data(&msg)),
                    static_cast<size_t>(zmq_msg_size(&msg)));

    if (options.verbose) {
        std::cerr << "[REQ] recv " << response << std::endl;
    }

    zmq_msg_close(&msg);
    zmq_close(sock);
    zmq_ctx_term(ctx);
    return true;
}
