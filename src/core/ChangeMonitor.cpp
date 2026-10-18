#include "core/ChangeMonitor.h"

#include <windows.h>

#include <array>
#include <filesystem>

#include "core/PathUtils.h"

namespace skillbox {

struct ChangeMonitor::WatchState {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::jthread thread;
};

ChangeMonitor::ChangeMonitor() = default;

ChangeMonitor::~ChangeMonitor() {
    Stop();
}

void ChangeMonitor::Start(
    std::vector<std::string> roots,
    Callback callback,
    std::chrono::milliseconds pollInterval) {
    Stop();

    running_ = true;
    {
        std::scoped_lock lock(mutex_);
        callback_ = std::move(callback);
    }

    for (const std::string& root : DeduplicatePaths(roots)) {
        const std::wstring rootPath = Utf8ToWide(root);
        HANDLE handle = CreateFileW(
            rootPath.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }

        auto state = std::make_unique<WatchState>();
        state->handle = handle;
        state->thread = std::jthread([this, handle](std::stop_token stop) {
            std::array<BYTE, 16 * 1024> buffer{};
            while (!stop.stop_requested() && running_) {
                DWORD bytes = 0;
                const BOOL changed = ReadDirectoryChangesW(
                    handle,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size()),
                    TRUE,
                    FILE_NOTIFY_CHANGE_FILE_NAME |
                        FILE_NOTIFY_CHANGE_DIR_NAME |
                        FILE_NOTIFY_CHANGE_LAST_WRITE |
                        FILE_NOTIFY_CHANGE_SIZE,
                    &bytes,
                    nullptr,
                    nullptr);
                if (!changed || bytes == 0) {
                    if (stop.stop_requested() || !running_) {
                        break;
                    }
                    continue;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                Callback callback;
                {
                    std::scoped_lock lock(mutex_);
                    callback = callback_;
                }
                if (callback) {
                    callback();
                }
            }
        });
        watchers_.push_back(std::move(state));
    }

    poller_ = std::jthread(
        [this, pollInterval](std::stop_token stop) {
            while (!stop.stop_requested() && running_) {
                for (int index = 0;
                     index < static_cast<int>(pollInterval.count() / 50) &&
                     !stop.stop_requested() && running_;
                     ++index) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (stop.stop_requested() || !running_) {
                    break;
                }
                Callback callback;
                {
                    std::scoped_lock lock(mutex_);
                    callback = callback_;
                }
                if (callback) {
                    callback();
                }
            }
        });
}

void ChangeMonitor::Stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (poller_.joinable()) {
        poller_.request_stop();
    }

    for (const auto& watcher : watchers_) {
        if (watcher->handle != INVALID_HANDLE_VALUE) {
            CancelIoEx(watcher->handle, nullptr);
        }
    }
    for (const auto& watcher : watchers_) {
        if (watcher->thread.joinable()) {
            watcher->thread.request_stop();
            watcher->thread.join();
        }
        if (watcher->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(watcher->handle);
            watcher->handle = INVALID_HANDLE_VALUE;
        }
    }
    watchers_.clear();

    if (poller_.joinable()) {
        poller_.join();
    }
    std::scoped_lock lock(mutex_);
    callback_ = {};
}

size_t ChangeMonitor::NativeWatchCount() const {
    return watchers_.size();
}

}  // namespace skillbox

