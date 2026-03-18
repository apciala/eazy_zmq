#pragma once

#include <string>

#include <json/json.h>

struct ReqRepOptions {
    std::string endpoint;
    int timeout_ms = 3000;
    bool verbose = false;
};

bool SendAndReceiveJson(const ReqRepOptions& options,
                        const Json::Value& request,
                        std::string& response,
                        std::string& error);
