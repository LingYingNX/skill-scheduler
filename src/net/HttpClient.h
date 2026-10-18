#pragma once

#include <chrono>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace skillbox {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    std::chrono::milliseconds timeout = std::chrono::seconds(30);
};

struct HttpResponse {
    bool ok = false;
    int statusCode = 0;
    std::string body;
    std::string error;
};

HttpResponse SendHttp(
    const HttpRequest& request,
    std::stop_token stop = {});

}  // namespace skillbox

