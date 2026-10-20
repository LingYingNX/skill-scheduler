#pragma once

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

private:
    CommandResult HandleCommand(
        const std::string& command,
        const nlohmann::json& payload);
    nlohmann::json BuildState();
    void RefreshCatalog(bool notify);
    void SaveConfig();
    void RestartMonitor();
    CommandResult StartTranslationBatch(bool force, const std::string& providerId);

    std::unique_ptr<ConfigService> configService_;
    std::unique_ptr<MainWindow> window_;
    Bridge bridge_;
    ChangeMonitor monitor_;
    std::mutex stateMutex_;
    std::mutex jobsMutex_;
    std::vector<std::jthread> jobs_;
    AppConfig config_;
    CatalogSnapshot snapshot_;
    LoadResult loadResult_;
};

}  // namespace skillbox

