#pragma once

#include <string>

struct SubOptions {
    std::string endpoint;
    std::string topic = "v1/notify/";
    int timeout_ms = 1000;
    int count = 0;
    bool verbose = false;
};

int RunSubscriber(const SubOptions& options);
