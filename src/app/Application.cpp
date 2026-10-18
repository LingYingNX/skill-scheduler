#include "app/Application.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include "app/MainWindow.h"
#include "core/Hash.h"
#include "core/PathUtils.h"
#include "net/ModelClient.h"
#include "net/TranslationService.h"
#include "platform/Clipboard.h"
#include "platform/Dpapi.h"
#include "platform/FolderPicker.h"

namespace skillbox {
namespace {

#if SKILLBOX_E2E
std::string E2EScenario() {
    wchar_t value[32]{};
    const DWORD size = GetEnvironmentVariableW(
        L"SKILLBOX_E2E_SCENARIO", value, static_cast<DWORD>(std::size(value)));
    return size > 0 && size < std::size(value) ? WideToUtf8(value) : std::string{};
}

void RunE2EScenario(CommandResult result) {
    if (!result.ok) {
        OutputDebugStringW((L"E2E failure: " + Utf8ToWide(result.message)).c_str());
        return;
    }
    const std::string scenario = E2EScenario();
    const auto skill = result.data["skills"].empty()
                           ? nlohmann::json{}
                           : result.data["skills"][0];
    const std::string path = skill.value("path", std::string{});
    const std::string command = skill.value("command", std::string{});
    if (scenario == "copy") {
        SetClipboardText(command);
    } else if (scenario == "category") {
        std::filesystem::path journal = std::filesystem::current_path() / L"category-move.json";
        std::ofstream stream(journal, std::ios::binary);
        stream << nlohmann::json{{"path", path}}.dump();
    }
}
#endif

std::filesystem::path ExecutableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD size = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(size);
    return std::filesystem::path(buffer).parent_path();
}

nlohmann::json RootToJson(const RootStatus& root) {
    return {
        {"path", root.path},
        {"available", root.available},
        {"error", root.error},
    };
}

nlohmann::json SkillToJson(
    const SkillEntry& skill,
    const AppConfig& config) {
    const auto translation = config.translations.find(skill.path);
    const bool translationValid =
        translation != config.translations.end() &&
        translation->second.sourceHash == Sha256Hex(skill.description);
    return {
        {"path", skill.path},
        {"root", skill.root},
        {"name", skill.name},
        {"command", skill.command},
        {"description", skill.description},
        {"category", skill.category},
        {"warning", skill.warning},
        {"translation", translationValid ? translation->second.text : ""},
    };
}

ProviderConfig ProviderFrom(const ApiConfig& api, const std::string& key) {
    return {
        api.protocol,
        api.baseUrl,
        key,
        api.model,
    };
}

}  // namespace

Application::Application()
    : configService_(std::make_unique<ConfigService>(
          ExecutableDirectory() / L"config.json")),
      bridge_([this](const std::string& command, const nlohmann::json& payload) {
          return HandleCommand(command, payload);
      }) {}

Application::~Application() {
    monitor_.Stop();
    std::scoped_lock lock(jobsMutex_);
    for (auto& job : jobs_) {
        job.request_stop();
    }
    jobs_.clear();
}

int Application::Run(void* instance) {
    loadResult_ = configService_->Load();
    config_ = loadResult_.config;

    window_ = std::make_unique<MainWindow>(
        [this](const std::string& request) { return bridge_.Handle(request); });
    if (!window_->Create(instance, config_.ui.pinned)) {
        return 1;
    }
    window_->SetPinned(config_.ui.pinned);

    RefreshCatalog(false);
    RestartMonitor();
#if SKILLBOX_E2E
    const std::string scenario = E2EScenario();
    if (!scenario.empty()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (scenario == "copy") {
            const auto state = HandleCommand("catalog.getState", {});
            if (state.ok && !state.data["skills"].empty()) {
                SetClipboardText(state.data["skills"][0].value("command", std::string{}));
            }
        } else if (scenario == "category") {
            const auto state = HandleCommand("catalog.getState", {});
            if (state.ok && !state.data["skills"].empty()) {
                const std::string path = state.data["skills"][0].value("path", std::string{});
                const CommandResult moved = HandleCommand(
                    "category.set", {{"path", path}, {"category", "frontend"}});
                std::ofstream journal(
                    ExecutableDirectory() / L"category-move.json",
                    std::ios::binary);
                journal << nlohmann::json{{"success", moved.ok}, {"path", path}}.dump();
            }
        } else if (scenario == "openSettings") {
            window_->ExecuteScript(L"document.querySelector('#open-settings').click()");
        }
    }
#endif
    const int result = window_->Run();
    monitor_.Stop();
    return result;
}

CommandResult Application::HandleCommand(
    const std::string& command,
    const nlohmann::json& payload) {
    if (command == "app.ping") {
        return CommandResult::Success({{"version", "0.1.0"}});
    }
    if (command == "catalog.getState") {
        return CommandResult::Success(BuildState());
    }
    if (command == "category.set") {
        const std::string path = payload.value("path", std::string{});
        const std::string category = payload.value("category", std::string{});
        bool updated = false;
        if (path.empty() || category.empty()) {
            return CommandResult::Error("invalid_category", "invalid path or category");
        }
        {
            std::scoped_lock lock(stateMutex_);
            updated = SetSkillCategory(config_, path, category);
            if (!updated) {
                return CommandResult::Error("invalid_category", "分类不存在");
            }
            SaveConfig();
        }
        RefreshCatalog(true);
        return CommandResult::Success(BuildState());
    }
    if (command == "category.rename") {
        const std::string id = payload.value("id", std::string{});
        const std::string name = payload.value("name", std::string{});
        {
            std::scoped_lock lock(stateMutex_);
            if (!RenameCategory(config_, id, name)) {
                return CommandResult::Error("invalid_category_name", "分类名称无效或已存在");
            }
            SaveConfig();
        }
        return CommandResult::Success(BuildState());
    }
    if (command == "category.add") {
        const std::string name = payload.value("name", std::string{});
        std::string newId;
        {
            std::scoped_lock lock(stateMutex_);
            if (!AddCategory(config_, name, &newId)) {
                return CommandResult::Error("invalid_category_name", "分类名称无效或已存在");
            }
            SaveConfig();
        }
        return CommandResult::Success(BuildState());
    }
    if (command == "ui.setCollapsed") {
        {
            std::scoped_lock lock(stateMutex_);
            if (payload.contains("allCollapsed") && payload["allCollapsed"].is_boolean()) {
                config_.ui.allCollapsed = payload["allCollapsed"].get<bool>();
            }
            if (payload.contains("collapsedCategories") &&
                payload["collapsedCategories"].is_array()) {
                config_.ui.collapsedCategories.clear();
                for (const auto& value : payload["collapsedCategories"]) {
                    if (value.is_string()) {
                        const std::string category = value.get<std::string>();
                        if (FindCategory(config_, category) &&
                            std::find(
                                config_.ui.collapsedCategories.begin(),
                                config_.ui.collapsedCategories.end(),
                                category) == config_.ui.collapsedCategories.end()) {
                            config_.ui.collapsedCategories.push_back(category);
                        }
                    }
                }
            }
            if (payload.contains("category") && payload["category"].is_string()) {
                const std::string category = payload["category"].get<std::string>();
                const bool collapsed = payload.value("collapsed", false);
                auto& categories = config_.ui.collapsedCategories;
                categories.erase(
                    std::remove(categories.begin(), categories.end(), category),
                    categories.end());
                if (collapsed && FindCategory(config_, category)) {
                    categories.push_back(category);
                }
            }
            SaveConfig();
        }
        return CommandResult::Success(BuildState());
    }
    if (command == "settings.addPath") {
        const std::string normalized =
            NormalizePathUtf8(payload.value("path", std::string{}));
        std::error_code error;
        if (normalized.empty() || !std::filesystem::is_directory(Utf8ToWide(normalized), error)) {
            return CommandResult::Error("invalid_path", "路径不存在或不是文件夹");
        }
        {
            std::scoped_lock lock(stateMutex_);
            const bool exists = std::any_of(
                config_.paths.begin(), config_.paths.end(),
                [&](const std::string& path) { return SamePath(path, normalized); });
            if (exists) {
                return CommandResult::Error("duplicate_path", "该路径已存在");
            }
            config_.paths.push_back(normalized);
            SaveConfig();
        }
        RestartMonitor();
        RefreshCatalog(true);
        return CommandResult::Success(BuildState());
    }
    if (command == "settings.removePath") {
        const std::string normalized =
            NormalizePathUtf8(payload.value("path", std::string{}));
        {
            std::scoped_lock lock(stateMutex_);
            const auto before = config_.paths.size();
            config_.paths.erase(
                std::remove_if(
                    config_.paths.begin(), config_.paths.end(),
                    [&](const std::string& path) { return SamePath(path, normalized); }),
                config_.paths.end());
            if (config_.paths.size() == before) {
                return CommandResult::Error("path_not_found", "路径不存在");
            }
            SaveConfig();
        }
        RestartMonitor();
        RefreshCatalog(true);
        return CommandResult::Success(BuildState());
    }
    if (command == "settings.choosePath") {
        const auto path = PickFolder(window_->NativeHandle());
        return path ? CommandResult::Success({{"path", *path}})
                    : CommandResult::Success({{"path", ""}});
    }
    if (command == "settings.updateApi") {
        const std::string protocol = payload.value("protocol", std::string{});
        const std::string baseUrl = payload.value("baseUrl", std::string{});
        const std::string model = payload.value("model", std::string{});
        if (protocol != "openai" && protocol != "anthropic") {
            return CommandResult::Error("invalid_protocol", "不支持的 API 协议");
        }
        if (baseUrl.empty()) {
            return CommandResult::Error("invalid_base_url", "Base URL 不能为空");
        }
        {
            std::scoped_lock lock(stateMutex_);
            config_.api.protocol = protocol;
            config_.api.baseUrl = baseUrl;
            config_.api.model = model;
            if (payload.contains("apiKey") && payload["apiKey"].is_string()) {
                const std::string plain = payload["apiKey"].get<std::string>();
                if (!plain.empty()) {
                    const std::string cipher = ProtectSecret(plain);
                    if (cipher.empty()) {
                        return CommandResult::Error(
                            "key_encryption_failed", "无法加密 API Key");
                    }
                    config_.api.apiKeyCipher = cipher;
                }
            }
            SaveConfig();
        }
        const nlohmann::json state = BuildState();
        return CommandResult::Success({{"api", state["api"]}});
    }
    if (command == "models.list") {
        ApiConfig api;
        {
            std::scoped_lock lock(stateMutex_);
            api = config_.api;
        }
        const auto key = UnprotectSecret(api.apiKeyCipher);
        if (!key || key->empty()) {
            return CommandResult::Error("api_key_missing", "请重新输入 API Key");
        }
        const ModelsResult result =
            ModelClient{}.ListModels(ProviderFrom(api, *key));
        return result.ok
                   ? CommandResult::Success({{"models", result.models}})
                   : CommandResult::Error("provider_error", result.error);
    }
    if (command == "models.test") {
        ApiConfig api;
        {
            std::scoped_lock lock(stateMutex_);
            api = config_.api;
        }
        const auto key = UnprotectSecret(api.apiKeyCipher);
        if (!key || key->empty()) {
            return CommandResult::Error("api_key_missing", "请重新输入 API Key");
        }
        const auto started = std::chrono::steady_clock::now();
        const CompletionResult result = ModelClient{}.Complete(
            ProviderFrom(api, *key),
            "Return the single word OK.",
            "OK",
            8);
        const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        return result.ok
                   ? CommandResult::Success({{"latencyMs", latency.count()}})
                   : CommandResult::Error("provider_error", result.error);
    }
    if (command == "translation.batch") {
        return StartTranslationBatch(payload.value("force", false));
    }
    if (command == "translation.one") {
        ApiConfig api;
        std::vector<SkillEntry> skills;
        {
            std::scoped_lock lock(stateMutex_);
            api = config_.api;
            skills = snapshot_.skills;
        }
        const auto key = UnprotectSecret(api.apiKeyCipher);
        if (!key || key->empty()) {
            return CommandResult::Error("api_key_missing", "请重新输入 API Key");
        }

        const ProviderConfig provider = ProviderFrom(api, *key);
        ModelClient client;
        const std::string path = payload.value("path", std::string{});
        const auto skill = std::find_if(
            skills.begin(), skills.end(),
            [&](const SkillEntry& item) { return item.path == path; });
        if (skill == skills.end()) {
            return CommandResult::Error("skill_not_found", "Skill 不存在");
        }
        if (LooksChinese(skill->description)) {
            return CommandResult::Error("already_chinese", "描述已经是中文");
        }
        const CompletionResult result =
            TranslateDescription(client, provider, skill->description);
        if (!result.ok) {
            return CommandResult::Error("provider_error", result.error);
        }
        {
            std::scoped_lock lock(stateMutex_);
            config_.translations[skill->path] =
                MakeTranslationEntry(skill->description, result.text);
            SaveConfig();
        }
        return CommandResult::Success({{"translated", 1}, {"failed", 0}, {"skipped", 0}});
    }
    if (command == "clipboard.copy") {
        const std::string text = payload.value("text", std::string{});
        if (text.empty()) {
            return CommandResult::Error("invalid_text", "text is required");
        }
        return SetClipboardText(text)
                   ? CommandResult::Success({{"text", text}})
                   : CommandResult::Error("clipboard_error", "unable to open the clipboard");
    }
    if (command == "window.drag") {
        window_->Drag();
        return CommandResult::Success();
    }
    if (command == "window.resizeStart") {
        const std::string direction =
            payload.value("direction", std::string{"bottom-right"});
#if SKILLBOX_E2E
        std::ofstream(
            ExecutableDirectory() / L"resize-command.txt",
            std::ios::binary)
            << direction;
#endif
        window_->StartResize(direction);
        return CommandResult::Success();
    }
    if (command == "window.minimize") {
        window_->Minimize();
        return CommandResult::Success();
    }
    if (command == "window.toggleMaximize") {
        window_->ToggleMaximize();
        return CommandResult::Success();
    }
    if (command == "window.close") {
        window_->Close();
        return CommandResult::Success();
    }
    if (command == "window.pin") {
        std::scoped_lock lock(stateMutex_);
        config_.ui.pinned = !window_->IsPinned();
        window_->SetPinned(config_.ui.pinned);
        SaveConfig();
        return CommandResult::Success({{"pinned", config_.ui.pinned}});
    }
    return CommandResult::Error(
        "unknown_command",
        "unknown command: " + command);
}

nlohmann::json Application::BuildState() {
    std::scoped_lock lock(stateMutex_);
    nlohmann::json skills = nlohmann::json::array();
    for (const SkillEntry& skill : snapshot_.skills) {
        skills.push_back(SkillToJson(skill, config_));
    }
    nlohmann::json roots = nlohmann::json::array();
    for (const RootStatus& root : snapshot_.roots) {
        roots.push_back(RootToJson(root));
    }
    return {
        {"skills", std::move(skills)},
        {"roots", std::move(roots)},
        {"paths", config_.paths},
        {"categoryDefinitions", config_.categoryDefinitions},
        {"api",
         {
             {"protocol", config_.api.protocol},
             {"baseUrl", config_.api.baseUrl},
             {"model", config_.api.model},
             {"keySaved", !config_.api.apiKeyCipher.empty()},
         }},
        {"ui",
         {
             {"pinned", config_.ui.pinned},
             {"allCollapsed", config_.ui.allCollapsed},
             {"collapsedCategories", config_.ui.collapsedCategories},
         }},
        {"warning", loadResult_.warning},
        {"monitorDegraded", !config_.paths.empty() && monitor_.NativeWatchCount() == 0},
    };
}

void Application::RefreshCatalog(bool notify) {
    std::vector<std::string> paths;
    std::unordered_map<std::string, std::string> categories;
    std::vector<CategoryDefinition> definitions;
    {
        std::scoped_lock lock(stateMutex_);
        paths = config_.paths;
        categories = config_.categories;
        definitions = config_.categoryDefinitions;
    }

    CatalogSnapshot refreshed = ScanSkills(paths, categories, definitions);
    bool changed = false;
    {
        std::scoped_lock lock(stateMutex_);
        changed = refreshed.signature != snapshot_.signature;
        snapshot_ = std::move(refreshed);
        const size_t beforeCategories = config_.categories.size();
        const size_t beforeTranslations = config_.translations.size();
        ReconcileCatalogConfig(config_, snapshot_);
        if (beforeCategories != config_.categories.size() ||
            beforeTranslations != config_.translations.size()) {
            SaveConfig();
            changed = true;
        }
    }
    if (notify && changed && window_) {
        window_->PostEvent("catalogChanged");
    }
}

void Application::SaveConfig() {
    configService_->Save(config_);
}

void Application::RestartMonitor() {
    std::vector<std::string> paths;
    {
        std::scoped_lock lock(stateMutex_);
        paths = config_.paths;
    }
    monitor_.Start(
        paths,
        [this] { RefreshCatalog(true); },
        std::chrono::seconds(1));
}

CommandResult Application::StartTranslationBatch(bool force) {
    ApiConfig api;
    std::vector<SkillEntry> skills;
    std::unordered_map<std::string, TranslationEntry> existing;
    {
        std::scoped_lock lock(stateMutex_);
        api = config_.api;
        skills = snapshot_.skills;
        existing = config_.translations;
    }
    const auto key = UnprotectSecret(api.apiKeyCipher);
    if (!key || key->empty()) {
        return CommandResult::Error("api_key_missing", "请重新输入 API Key");
    }

    const ProviderConfig provider = ProviderFrom(api, *key);
    std::vector<TranslationCandidate> candidates;
    candidates.reserve(skills.size());
    for (const SkillEntry& skill : skills) {
        candidates.push_back({skill.path, skill.description});
    }

    std::scoped_lock lock(jobsMutex_);
    jobs_.emplace_back(
        [this, provider, candidates = std::move(candidates), existing = std::move(existing), force](
            std::stop_token stop) {
            ModelClient client;
            TranslationBatchResult result;
            for (const TranslationCandidate& candidate : candidates) {
                if (stop.stop_requested()) {
                    break;
                }
                TranslationBatchResult one = RunTranslationBatch(
                    {candidate},
                    existing,
                    force,
                    [&](const std::string& description) {
                        return TranslateDescription(client, provider, description);
                    });
                result.translated += one.translated;
                result.failed += one.failed;
                result.skipped += one.skipped;
                for (const auto& [path, entry] : one.entries) {
                    {
                        std::scoped_lock stateLock(stateMutex_);
                        config_.translations[path] = entry;
                        SaveConfig();
                    }
                    result.entries[path] = entry;
                }
                if (window_) {
                    window_->PostEvent(
                        "translationProgress",
                        {
                            {"completed", result.translated + result.failed + result.skipped},
                            {"total", candidates.size()},
                            {"translated", result.translated},
                            {"failed", result.failed},
                        });
                }
            }
            if (window_) {
                window_->PostEvent(
                    "translationFinished",
                    {
                        {"translated", result.translated},
                        {"failed", result.failed},
                        {"skipped", result.skipped},
                    });
            }
        });
    return CommandResult::Success({{"accepted", true}});
}

}  // namespace skillbox
