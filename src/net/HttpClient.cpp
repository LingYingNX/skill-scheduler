#include "net/HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cwctype>
#include <thread>

#include "core/PathUtils.h"

namespace skillbox {
namespace {

constexpr size_t kMaxResponseBytes = 8 * 1024 * 1024;

struct InternetHandle {
    HINTERNET value = nullptr;
    ~InternetHandle() {
        if (value) {
            WinHttpCloseHandle(value);
        }
    }
};

std::string WinHttpError(const char* stage) {
    return std::string(stage) + " failed (" + std::to_string(GetLastError()) + ")";
}

}  // namespace

HttpResponse SendHttp(
    const HttpRequest& request,
    std::stop_token stop) {
    HttpResponse response;
    if (stop.stop_requested()) {
        response.error = "request cancelled";
        return response;
    }

    const std::wstring url = Utf8ToWide(request.url);
    if (url.empty()) {
        response.error = "invalid URL";
        return response;
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &components)) {
        response.error = "invalid URL";
        return response;
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    if (components.nScheme != INTERNET_SCHEME_HTTPS) {
        std::wstring lowered = host;
        std::transform(
            lowered.begin(), lowered.end(), lowered.begin(),
            [](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
        const bool loopback =
            lowered == L"localhost" || lowered == L"127.0.0.1" ||
            lowered == L"::1" || lowered == L"[::1]";
        if (!loopback) {
            response.error = "only HTTPS endpoints are allowed";
            return response;
        }
    }
    std::wstring target(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        target.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }

    InternetHandle session(WinHttpOpen(
        L"SkillBox/0.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!session.value) {
        response.error = WinHttpError("WinHttpOpen");
        return response;
    }
    WinHttpSetTimeouts(
        session.value,
        static_cast<int>(request.timeout.count()),
        static_cast<int>(request.timeout.count()),
        static_cast<int>(request.timeout.count()),
        static_cast<int>(request.timeout.count()));

    InternetHandle connection(WinHttpConnect(
        session.value,
        host.c_str(),
        static_cast<INTERNET_PORT>(components.nPort),
        0));
    if (!connection.value) {
        response.error = WinHttpError("WinHttpConnect");
        return response;
    }

    const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    const std::wstring method = Utf8ToWide(request.method);
    HINTERNET rawRequest = WinHttpOpenRequest(
        connection.value,
        method.c_str(),
        target.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (!rawRequest) {
        response.error = WinHttpError("WinHttpOpenRequest");
        return response;
    }

    std::atomic<HINTERNET> activeRequest{rawRequest};
    std::atomic_bool timedOut = false;
    std::atomic_bool cancelled = false;
    std::atomic_bool completed = false;
    std::jthread watchdog([&](std::stop_token watchdogStop) {
        const auto deadline = std::chrono::steady_clock::now() + request.timeout;
        while (!watchdogStop.stop_requested() && !completed) {
            if (stop.stop_requested()) {
                cancelled = true;
            } else if (std::chrono::steady_clock::now() >= deadline) {
                timedOut = true;
            }
            if (cancelled || timedOut) {
                if (HINTERNET handle = activeRequest.exchange(nullptr)) {
                    WinHttpCloseHandle(handle);
                }
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    struct RequestGuard {
        std::atomic_bool& completed;
        std::jthread& watchdog;
        std::atomic<HINTERNET>& activeRequest;
        ~RequestGuard() {
            completed = true;
            watchdog.request_stop();
            if (watchdog.joinable()) {
                watchdog.join();
            }
            if (HINTERNET handle = activeRequest.exchange(nullptr)) {
                WinHttpCloseHandle(handle);
            }
        }
    } requestGuard{completed, watchdog, activeRequest};

    std::wstring headers;
    for (const auto& [name, value] : request.headers) {
        headers += Utf8ToWide(name + ": " + value + "\r\n");
    }

    if (stop.stop_requested()) {
        response.error = "request cancelled";
        return response;
    }

    HINTERNET httpRequest = activeRequest.load();
    if (!httpRequest) {
        response.error = timedOut ? "request timed out" : "request cancelled";
        return response;
    }
    if (!WinHttpSendRequest(
            httpRequest,
            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
            headers.empty() ? 0 : static_cast<DWORD>(headers.size()),
            request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data()),
            static_cast<DWORD>(request.body.size()),
            static_cast<DWORD>(request.body.size()),
            0)) {
        response.error = timedOut ? "request timed out"
                                  : cancelled ? "request cancelled"
                                              : WinHttpError("WinHttpSendRequest");
        return response;
    }

    if (!WinHttpReceiveResponse(httpRequest, nullptr)) {
        response.error = timedOut ? "request timed out"
                                  : cancelled ? "request cancelled"
                                              : WinHttpError("WinHttpReceiveResponse");
        return response;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(
            httpRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX)) {
        response.error = WinHttpError("WinHttpQueryHeaders");
        return response;
    }
    response.statusCode = static_cast<int>(status);

    std::array<char, 16 * 1024> buffer{};
    while (!stop.stop_requested()) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(httpRequest, &available)) {
            response.error = timedOut ? "request timed out"
                                      : cancelled ? "request cancelled"
                                                  : WinHttpError("WinHttpQueryDataAvailable");
            return response;
        }
        if (available == 0) {
            break;
        }
        while (available > 0) {
            const DWORD chunk =
                std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
            DWORD read = 0;
            if (!WinHttpReadData(httpRequest, buffer.data(), chunk, &read)) {
                response.error = timedOut ? "request timed out"
                                          : cancelled ? "request cancelled"
                                                      : WinHttpError("WinHttpReadData");
                return response;
            }
            if (read == 0) {
                break;
            }
            response.body.append(buffer.data(), read);
            if (response.body.size() > kMaxResponseBytes) {
                response.error = "response body too large";
                return response;
            }
            available -= read;
        }
    }

    if (stop.stop_requested() || cancelled) {
        response.error = "request cancelled";
        return response;
    }
    if (timedOut) {
        response.error = "request timed out";
        return response;
    }
    response.ok = response.statusCode >= 200 && response.statusCode < 300;
    if (!response.ok && response.error.empty()) {
        response.error = "HTTP " + std::to_string(response.statusCode);
    }
    return response;
}

}  // namespace skillbox

