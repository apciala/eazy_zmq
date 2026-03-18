#include "sub_client.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

#include <zmq.h>

namespace {

std::string ErrnoToString() {
    const int e = zmq_errno();
    const char* txt = zmq_strerror(e);
    return std::string(txt ? txt : "unknown") + " (" + std::to_string(e) + ")";
}

}  // namespace

int RunSubscriber(const SubOptions& options) {
    void* ctx = zmq_ctx_new();
    if (!ctx) {
        std::cerr << "SUB error: zmq_ctx_new failed" << std::endl;
        return 10;
    }

    void* sock = zmq_socket(ctx, ZMQ_SUB);
    if (!sock) {
        std::cerr << "SUB error: zmq_socket(ZMQ_SUB) failed" << std::endl;
        zmq_ctx_term(ctx);
        return 11;
    }

    if (zmq_setsockopt(sock, ZMQ_SUBSCRIBE, options.topic.data(), options.topic.size()) != 0) {
        std::cerr << "SUB error: set ZMQ_SUBSCRIBE failed: " << ErrnoToString() << std::endl;
        zmq_close(sock);
        zmq_ctx_term(ctx);
        return 12;
    }

    if (options.timeout_ms > 0) {
        if (zmq_setsockopt(sock, ZMQ_RCVTIMEO, &options.timeout_ms, sizeof(options.timeout_ms)) != 0) {
            std::cerr << "SUB warn: set ZMQ_RCVTIMEO failed: " << ErrnoToString() << std::endl;
        }
    }

    if (options.verbose) {
        std::cerr << "[SUB] connect " << options.endpoint << std::endl;
        std::cerr << "[SUB] topic   " << options.topic << std::endl;
    }

    if (zmq_connect(sock, options.endpoint.c_str()) != 0) {
        std::cerr << "SUB error: connect failed: " << ErrnoToString() << std::endl;
        zmq_close(sock);
        zmq_ctx_term(ctx);
        return 13;
    }

    int received = 0;
    while (options.count <= 0 || received < options.count) {
        zmq_msg_t msg;
        zmq_msg_init(&msg);
        const int rc = zmq_msg_recv(&msg, sock, 0);
        if (rc < 0) {
            if (zmq_errno() == EAGAIN) {
                zmq_msg_close(&msg);
                continue;
            }
            std::cerr << "SUB error: recv failed: " << ErrnoToString() << std::endl;
            zmq_msg_close(&msg);
            zmq_close(sock);
            zmq_ctx_term(ctx);
            return 14;
        }

        std::string payload(static_cast<const char*>(zmq_msg_data(&msg)),
                            static_cast<size_t>(zmq_msg_size(&msg)));
        std::cout << payload << std::endl;
        ++received;
        zmq_msg_close(&msg);
    }

    zmq_close(sock);
    zmq_ctx_term(ctx);
    return 0;
}
