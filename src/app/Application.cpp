#include "app/Application.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
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
        TranslationIsValid(translation->second, skill.description);
    const std::string translationText =
        translationValid ? NormalizeTranslation(translation->second.text) : "";
    const auto translationEn = config.translationsEn.find(skill.path);
    const bool translationEnValid =
        translationEn != config.translationsEn.end() &&
        EnglishTranslationIsValid(translationEn->second, skill.description);
    const std::string translationEnText =
        translationEnValid ? NormalizeTranslation(translationEn->second.text) : "";
    return {
        {"path", skill.path},
        {"root", skill.root},
        {"name", skill.name},
        {"command", skill.command},
        {"description", skill.description},
        {"category", skill.category},
        {"warning", skill.warning},
        {"translation", translationText},
        {"translationEn", translationEnText},
        {"sourceLang", LooksChinese(skill.description) ? "zh" : "en"},
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

bool IsSupportedProtocol(const std::string& protocol) {
    return protocol == "openai" || protocol == "openai-responses" ||
           protocol == "anthropic";
}

std::string TrimText(std::string value) {
    const auto first = std::find_if_not(
        value.begin(), value.end(),
        [](unsigned char character) { return std::isspace(character); });
    const auto last = std::find_if_not(
        value.rbegin(), value.rend(),
        [](unsigned char character) { return std::isspace(character); }).base();
    return first < last ? std::string(first, last) : std::string{};
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
        job.thread.request_stop();
    }
    jobs_.clear();
}

int Application::Run(void* instance) {
    loadResult_ = configService_->Load();
    config_ = loadResult_.config;
    if (loadResult_.saveRequired && !configService_->Save(config_)) {
        loadResult_.warning = "配置迁移保存失败，本次修改可能无法保留";
    }

    window_ = std::make_unique<MainWindow>(
        [this](const std::string& request) { return HandleRequest(request); });
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

std::string Application::HandleRequest(const std::string& request) {
    try {
        const nlohmann::json message = nlohmann::json::parse(request);
        const std::string command = message.value("command", std::string{});
        if (command == "models.list" || command == "models.test" ||
            command == "translation.one") {
            QueueNetworkJob(
                message.value("id", std::string{}),
                command,
                message.contains("payload") ? message["payload"]
                                            : nlohmann::json::object());
            return {};
        }
    } catch (const std::exception&) {
        // Bridge produces the canonical error response for malformed requests.
    }
    return bridge_.Handle(request);
}

void Application::StartJob(std::function<void(std::stop_token)> work) {
    std::scoped_lock lock(jobsMutex_);
    jobs_.erase(
        std::remove_if(
            jobs_.begin(), jobs_.end(),
            [](Job& job) {
                if (!job.done || !job.done->load()) {
                    return false;
                }
                if (job.thread.joinable()) {
                    job.thread.join();
                }
                return true;
            }),
        jobs_.end());

    auto done = std::make_shared<std::atomic_bool>(false);
    jobs_.push_back({
        std::jthread([done, work = std::move(work)](std::stop_token stop) {
            work(stop);
            done->store(true);
        }),
        done,
    });
}

void Application::QueueNetworkJob(
    const std::string& id,
    const std::string& command,
    nlohmann::json payload) {
    StartJob([this, id, command, payload = std::move(payload)](std::stop_token) {
        CommandResult result;
        try {
            result = HandleCommand(command, payload);
        } catch (const std::exception& exception) {
            result = CommandResult::Error("internal_error", exception.what());
        }
        if (!window_) {
            return;
        }
        window_->PostRawMessage(
            result.ok ? Bridge::Success(id, result.data)
                      : Bridge::Error(id, result.code, result.message));
    });
}

CommandResult Application::HandleCommand(
    const std::string& command,
    const nlohmann::json& payload) {
    if (command == "app.ping") {
        return CommandResult::Success({{"version", "1.0"}});
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
            if (!CommitConfig(config_)) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
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
            if (!CommitConfig(config_)) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
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
            if (!CommitConfig(config_)) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
        }
        return CommandResult::Success(BuildState());
    }
    if (command == "category.remove") {
        const std::string id = payload.value("id", std::string{});
        if (id.empty()) {
            return CommandResult::Error("invalid_category", "分类不存在");
        }
        {
            std::scoped_lock lock(stateMutex_);
            AppConfig next = config_;
            if (!DeleteCategory(next, id)) {
                return CommandResult::Error(
                    "category_protected", "分类不存在或不可删除");
            }
            if (!CommitConfig(std::move(next))) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
        }
        RefreshCatalog(true);
        return CommandResult::Success(BuildState());
    }
    if (command == "category.reorder") {
        std::vector<std::string> ids;
        if (payload.contains("ids") && payload["ids"].is_array()) {
            for (const auto& value : payload["ids"]) {
                if (value.is_string()) {
                    ids.push_back(value.get<std::string>());
                }
            }
        }
        {
            std::scoped_lock lock(stateMutex_);
            AppConfig next = config_;
            if (!ReorderCategories(next, ids)) {
                return CommandResult::Error("invalid_category_order", "分类排序无效");
            }
            if (!CommitConfig(std::move(next))) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
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
            if (!CommitConfig(config_)) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
        }
        return CommandResult::Success(BuildState());
    }
    if (command == "ui.setTheme") {
        const std::string theme = payload.value("theme", std::string{});
        if (theme != "light" && theme != "dark") {
            return CommandResult::Error("invalid_theme", "主题无效");
        }
        {
            std::scoped_lock lock(stateMutex_);
            config_.ui.theme = theme;
            if (!CommitConfig(config_)) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
        }
        return CommandResult::Success({{"theme", theme}});
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
            if (!CommitConfig(config_)) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
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
            if (!CommitConfig(config_)) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
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
    if (command == "providers.update" || command == "settings.updateApi") {
        const std::string requestedId = payload.value("id", std::string{});
        const std::string protocol = payload.value("protocol", std::string{});
        const std::string baseUrl = payload.value("baseUrl", std::string{});
        const std::string model = payload.value("model", std::string{});
        if (!IsSupportedProtocol(protocol)) {
            return CommandResult::Error("invalid_protocol", "不支持的 API 协议");
        }
        {
            std::scoped_lock lock(stateMutex_);
            const std::string id = requestedId;
            if (id.empty()) {
                return CommandResult::Error(
                    "provider_required", "请选择模型供应商");
            }
            ApiConfig* provider = FindApiProvider(config_, id);
            if (!provider) {
                return CommandResult::Error(
                    "provider_not_found", "模型供应商不存在");
            }
            provider->protocol = protocol;
            provider->baseUrl = baseUrl;
            provider->model = model;
            if (payload.contains("name") && payload["name"].is_string()) {
                const std::string name =
                    TrimText(payload["name"].get<std::string>());
                if (name.empty() || name.size() > 32) {
                    return CommandResult::Error(
                        "invalid_provider_name", "供应商名称无效");
                }
                provider->name = name;
            }
            if (payload.contains("apiKey") && payload["apiKey"].is_string()) {
                const std::string plain = payload["apiKey"].get<std::string>();
                if (!plain.empty()) {
                    const std::string cipher = ProtectSecret(plain);
                    if (cipher.empty()) {
                        return CommandResult::Error(
                            "key_encryption_failed", "无法加密 API Key");
                    }
                    provider->apiKeyCipher = cipher;
                }
            }
            if (!CommitConfig(config_)) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
        }
        return CommandResult::Success(BuildState());
    }
    if (command == "providers.add") {
        const std::string id = payload.value("id", std::string{});
        {
            std::scoped_lock lock(stateMutex_);
            if (FindApiProvider(config_, id)) {
                if (std::find(
                        config_.enabledProviderIds.begin(),
                        config_.enabledProviderIds.end(),
                        id) == config_.enabledProviderIds.end()) {
                    config_.enabledProviderIds.push_back(id);
                }
            } else {
                const auto presets = DefaultApiProviders();
                const auto preset = presets.find(id);
                if (preset == presets.end()) {
                    return CommandResult::Error(
                        "provider_not_found", "模型供应商不存在");
                }
                config_.providers[id] = preset->second;
                config_.enabledProviderIds.push_back(id);
            }
            if (!CommitConfig(config_)) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
        }
        return CommandResult::Success(BuildState());
    }
    if (command == "providers.remove") {
        const std::string id = payload.value("id", std::string{});
        {
            std::scoped_lock lock(stateMutex_);
            config_.enabledProviderIds.erase(
                std::remove(
                    config_.enabledProviderIds.begin(),
                    config_.enabledProviderIds.end(),
                    id),
                config_.enabledProviderIds.end());
            config_.providers.erase(id);
            if (!CommitConfig(config_)) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
        }
        return CommandResult::Success(BuildState());
    }
    if (command == "models.list") {
        ApiConfig api;
        const std::string requestedId = payload.value("providerId", std::string{});
        {
            std::scoped_lock lock(stateMutex_);
            const std::string id = requestedId;
            if (id.empty()) {
                return CommandResult::Error(
                    "provider_required", "请选择模型供应商");
            }
            const ApiConfig* provider = FindApiProvider(config_, id);
            if (!provider) {
                return CommandResult::Error(
                    "provider_not_found", "模型供应商不存在");
            }
            api = *provider;
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
        const std::string requestedId = payload.value("providerId", std::string{});
        {
            std::scoped_lock lock(stateMutex_);
            const std::string id = requestedId;
            if (id.empty()) {
                return CommandResult::Error(
                    "provider_required", "请选择模型供应商");
            }
            const ApiConfig* provider = FindApiProvider(config_, id);
            if (!provider) {
                return CommandResult::Error(
                    "provider_not_found", "模型供应商不存在");
            }
            api = *provider;
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
        return StartTranslationBatch(
            payload.value("force", false),
            payload.value("providerId", std::string{}));
    }
    if (command == "translation.one") {
        ApiConfig api;
        std::vector<SkillEntry> skills;
        const std::string providerId =
            payload.value("providerId", std::string{});
        {
            std::scoped_lock lock(stateMutex_);
            const ApiConfig* provider = FindApiProvider(config_, providerId);
            if (!provider) {
                return CommandResult::Error(
                    "provider_not_found", "模型供应商不存在");
            }
            api = *provider;
            skills = snapshot_.skills;
        }
        const auto key = UnprotectSecret(api.apiKeyCipher);
        if (!key || key->empty()) {
            return CommandResult::Error("api_key_missing", "请重新输入 API Key");
        }

        const ProviderConfig provider = ProviderFrom(api, *key);
        ModelClient client;
        const std::string path = payload.value("path", std::string{});
        const std::string target = payload.value("target", std::string{"zh"});
        if (target != "zh" && target != "en") {
            return CommandResult::Error("invalid_target", "翻译目标语言无效");
        }
        const auto skill = std::find_if(
            skills.begin(), skills.end(),
            [&](const SkillEntry& item) { return item.path == path; });
        if (skill == skills.end()) {
            return CommandResult::Error("skill_not_found", "Skill 不存在");
        }
        const bool sourceChinese = LooksChinese(skill->description);
        if (target == "zh" && sourceChinese) {
            return CommandResult::Error("already_chinese", "描述已经是中文");
        }
        if (target == "en" && !sourceChinese) {
            return CommandResult::Error("already_english", "描述已经是英文");
        }
        const CompletionResult result =
            target == "en"
                ? TranslateDescriptionToEnglish(client, provider, skill->description)
                : TranslateDescription(client, provider, skill->description);
        if (!result.ok) {
            return CommandResult::Error("provider_error", result.error);
        }
        {
            std::scoped_lock lock(stateMutex_);
            AppConfig next = config_;
            auto& cache = target == "en" ? next.translationsEn : next.translations;
            cache[skill->path] = MakeTranslationEntry(skill->description, result.text);
            if (!CommitConfig(std::move(next))) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
        }
        return CommandResult::Success(
            {{"translated", 1}, {"failed", 0}, {"skipped", 0}, {"target", target}});
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
            if (!CommitConfig(config_)) {
                return CommandResult::Error("config_save_failed", "配置保存失败");
            }
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
    nlohmann::json providers = nlohmann::json::array();
    for (const std::string& id : config_.enabledProviderIds) {
        const ApiConfig* found = FindApiProvider(config_, id);
        if (!found) {
            continue;
        }
        const ApiConfig& provider = *found;
        providers.push_back({
            {"id", id},
            {"protocol", provider.protocol},
            {"baseUrl", provider.baseUrl},
            {"model", provider.model},
            {"keySaved", !provider.apiKeyCipher.empty()},
            {"name", provider.name},
        });
    }
    return {
        {"skills", std::move(skills)},
        {"roots", std::move(roots)},
        {"paths", config_.paths},
        {"categoryDefinitions", config_.categoryDefinitions},
        {"providers", std::move(providers)},
        {"enabledProviderIds", config_.enabledProviderIds},
        {"ui",
         {
             {"pinned", config_.ui.pinned},
             {"allCollapsed", config_.ui.allCollapsed},
             {"collapsedCategories", config_.ui.collapsedCategories},
             {"theme", config_.ui.theme},
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
        AppConfig next = config_;
        const size_t beforeCategories = next.categories.size();
        const size_t beforeTranslations = next.translations.size();
        ReconcileCatalogConfig(next, snapshot_);
        if (beforeCategories != next.categories.size() ||
            beforeTranslations != next.translations.size()) {
            if (configService_->Save(next)) {
                config_ = std::move(next);
                changed = true;
            } else {
                loadResult_.warning = "配置保存失败，分类与翻译缓存已保留在内存中";
            }
        }
    }
    if (notify && changed && window_) {
        window_->PostEvent("catalogChanged");
    }
}

bool Application::CommitConfig(AppConfig config) {
    if (!configService_->Save(config)) {
        return false;
    }
    config_ = std::move(config);
    return true;
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

CommandResult Application::StartTranslationBatch(
    bool force,
    const std::string& providerId) {
    ApiConfig api;
    std::vector<SkillEntry> skills;
    std::unordered_map<std::string, TranslationEntry> existing;
    {
        std::scoped_lock lock(stateMutex_);
        const ApiConfig* provider = FindApiProvider(config_, providerId);
        if (!provider) {
            return CommandResult::Error(
                "provider_not_found", "模型供应商不存在");
        }
        api = *provider;
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

    if (translationRunning_.exchange(true)) {
        return CommandResult::Error("translation_busy", "已有翻译任务在进行");
    }

    StartJob(
        [this, provider, candidates = std::move(candidates), existing = std::move(existing), force](
            std::stop_token stop) {
            struct RunningGuard {
                std::atomic_bool& flag;
                ~RunningGuard() { flag = false; }
            } runningGuard{translationRunning_};
            ModelClient client;
            TranslationBatchResult result;
            bool saveFailed = false;
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
                    bool saved = false;
                    {
                        std::scoped_lock stateLock(stateMutex_);
                        AppConfig next = config_;
                        next.translations[path] = entry;
                        saved = CommitConfig(std::move(next));
                    }
                    if (!saved) {
                        saveFailed = true;
                        break;
                    }
                    result.entries[path] = entry;
                }
                if (saveFailed) {
                    ++result.failed;
                    break;
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
                        {"configSaveFailed", saveFailed},
                    });
            }
        });
    return CommandResult::Success({{"accepted", true}});
}

}  // namespace skillbox
