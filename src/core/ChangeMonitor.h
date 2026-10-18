#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace skillbox {

class ChangeMonitor {
public:
    using Callback = std::function<void()>;

    ChangeMonitor();
    ~ChangeMonitor();

    ChangeMonitor(const ChangeMonitor&) = delete;
    ChangeMonitor& operator=(const ChangeMonitor&) = delete;

    void Start(
        std::vector<std::string> roots,
        Callback callback,
        std::chrono::milliseconds pollInterval = std::chrono::seconds(1));
    void Stop();
    size_t NativeWatchCount() const;

private:
    struct WatchState;

    std::atomic_bool running_ = false;
    std::mutex mutex_;
    std::vector<std::unique_ptr<WatchState>> watchers_;
    std::jthread poller_;
    Callback callback_;
};

}  // namespace skillbox

