#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "bridge/Bridge.h"
#include "core/ChangeMonitor.h"
#include "core/Config.h"
#include "core/SkillCatalog.h"

namespace skillbox {

class MainWindow;

class Application {
public:
    Application();
    ~Application();

    int Run(void* instance);
    std::string HandleRequest(const std::string& request);

private:
    struct Job {
        std::jthread thread;
        std::shared_ptr<std::atomic_bool> done;
    };

    CommandResult HandleCommand(
        const std::string& command,
        const nlohmann::json& payload);
    void StartJob(std::function<void(std::stop_token)> work);
    void QueueNetworkJob(
        const std::string& id,
        const std::string& command,
        nlohmann::json payload);
    nlohmann::json BuildState();
    void RefreshCatalog(bool notify);
    bool CommitConfig(AppConfig config);
    void RestartMonitor();
    CommandResult StartTranslationBatch(bool force, const std::string& providerId);

    std::unique_ptr<ConfigService> configService_;
    std::unique_ptr<MainWindow> window_;
    Bridge bridge_;
    ChangeMonitor monitor_;
    std::mutex stateMutex_;
    std::mutex jobsMutex_;
    std::vector<Job> jobs_;
    std::atomic_bool translationRunning_{false};
    AppConfig config_;
    CatalogSnapshot snapshot_;
    LoadResult loadResult_;
};

}  // namespace skillbox

