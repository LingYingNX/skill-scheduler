#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <functional>
#include <string>
#include <thread>
#include <utility>

namespace skillbox::test {

class MockHttpServer {
public:
    using Handler = std::function<std::pair<int, std::string>(
        const std::string& method,
        const std::string& target,
        const std::string& headers,
        const std::string& body)>;

    explicit MockHttpServer(Handler handler) : handler_(std::move(handler)) {
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == INVALID_SOCKET) {
            return;
        }
        int enabled = 1;
        setsockopt(
            listener_, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<const char*>(&enabled), sizeof(enabled));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener_, SOMAXCONN) != 0) {
            closesocket(listener_);
            listener_ = INVALID_SOCKET;
            return;
        }

        int length = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::jthread([this](std::stop_token stop) {
            while (!stop.stop_requested()) {
                SOCKET client = accept(listener_, nullptr, nullptr);
                if (client == INVALID_SOCKET) {
                    break;
                }
                Serve(client);
                closesocket(client);
            }
        });
    }

    ~MockHttpServer() {
        thread_.request_stop();
        if (listener_ != INVALID_SOCKET) {
            shutdown(listener_, SD_BOTH);
            closesocket(listener_);
            listener_ = INVALID_SOCKET;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::string baseUrl() const {
        return "http://127.0.0.1:" + std::to_string(port_);
    }

    bool started() const {
        return listener_ != INVALID_SOCKET;
    }

private:
    void Serve(SOCKET client) {
        std::string request;
        char buffer[4096];
        size_t headerEnd = std::string::npos;
        size_t contentLength = 0;
        while (true) {
            const int received = recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                return;
            }
            request.append(buffer, received);
            if (headerEnd == std::string::npos) {
                headerEnd = request.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    const std::string headers = request.substr(0, headerEnd);
                    const std::string lower = Lower(headers);
                    const size_t marker = lower.find("content-length:");
                    if (marker != std::string::npos) {
                        const size_t start = marker + std::string("content-length:").size();
                        contentLength = static_cast<size_t>(
                            std::stoul(headers.substr(start)));
                    }
                }
            }
            if (headerEnd != std::string::npos &&
                request.size() >= headerEnd + 4 + contentLength) {
                break;
            }
        }

        const size_t firstSpace = request.find(' ');
        const size_t secondSpace =
            firstSpace == std::string::npos ? std::string::npos : request.find(' ', firstSpace + 1);
        const std::string method = request.substr(0, firstSpace);
        const std::string target = request.substr(
            firstSpace + 1, secondSpace - firstSpace - 1);
        const std::string headers = request.substr(0, headerEnd);
        const std::string body = request.substr(headerEnd + 4, contentLength);

        auto [status, responseBody] = handler_(method, target, headers, body);
        const std::string reason = status >= 200 && status < 300 ? "OK" : "Error";
        const std::string response =
            "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n" +
            "Content-Type: application/json\r\n" +
            "Content-Length: " + std::to_string(responseBody.size()) + "\r\n" +
            "Connection: close\r\n\r\n" + responseBody;
        send(client, response.data(), static_cast<int>(response.size()), 0);
    }

    static std::string Lower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    Handler handler_;
    SOCKET listener_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::jthread thread_;
};

}  // namespace skillbox::test
