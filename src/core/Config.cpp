#include "core/Config.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

#include "core/PathUtils.h"

namespace skillbox {
namespace {

std::string ReadTextFile(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    return text;
}

std::string Timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);
    std::ostringstream value;
    value << std::put_time(&local, "%Y%m%d-%H%M%S");
    return value.str();
}

std::unordered_map<std::string, std::string> ReadStringMap(
    const nlohmann::json& json,
    const char* key) {
    std::unordered_map<std::string, std::string> result;
    const auto it = json.find(key);
    if (it == json.end() || !it->is_object()) {
        return result;
    }
    for (auto entry = it->begin(); entry != it->end(); ++entry) {
        if (entry.value().is_string()) {
            result.emplace(entry.key(), entry.value().get<std::string>());
        }
    }
    return result;
}

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

}  // namespace

std::vector<CategoryDefinition> DefaultCategoryDefinitions() {
    return {
        {"favorite", "常用"},
        {"uncommon", "不常用"},
        {"frontend", "前端"},
        {"backend", "后端"},
        {"other", "其他"},
    };
}

AppConfig DefaultConfig() {
    AppConfig config;
    config.categoryDefinitions = DefaultCategoryDefinitions();
    const std::string skillsPath = DefaultSkillsPath();
    if (!skillsPath.empty()) {
        config.paths.push_back(skillsPath);
    }
    return config;
}

nlohmann::json ToJson(const AppConfig& config) {
    nlohmann::json translations = nlohmann::json::object();
    for (const auto& [path, entry] : config.translations) {
        translations[path] = {
            {"sourceHash", entry.sourceHash},
            {"text", entry.text},
            {"updatedAt", entry.updatedAt},
        };
    }

    return {
        {"version", config.version},
        {"paths", config.paths},
        {"categoryDefinitions", config.categoryDefinitions},
        {"categories", config.categories},
        {"translations", std::move(translations)},
        {"api",
         {
             {"protocol", config.api.protocol},
             {"baseUrl", config.api.baseUrl},
             {"model", config.api.model},
             {"apiKeyCipher", config.api.apiKeyCipher},
         }},
        {"ui",
         {
             {"pinned", config.ui.pinned},
             {"allCollapsed", config.ui.allCollapsed},
             {"collapsedCategories", config.ui.collapsedCategories},
         }},
    };
}

std::optional<AppConfig> FromJson(
    const nlohmann::json& json,
    std::string* error) {
    if (!json.is_object()) {
        if (error) {
            *error = "config root must be an object";
        }
        return std::nullopt;
    }

    AppConfig config = DefaultConfig();
    config.version = json.value("version", kConfigVersion);
    if (config.version != kConfigVersion) {
        if (error) {
            *error = "unsupported config version";
        }
        return std::nullopt;
    }

    config.paths.clear();
    if (const auto paths = json.find("paths"); paths != json.end() && paths->is_array()) {
        for (const auto& value : *paths) {
            if (value.is_string()) {
                config.paths.push_back(value.get<std::string>());
            }
        }
    }
    config.paths = DeduplicatePaths(config.paths);

    config.categoryDefinitions.clear();
    if (const auto definitions = json.find("categoryDefinitions");
        definitions != json.end() && definitions->is_array()) {
        std::set<std::string> seen;
        for (const auto& value : *definitions) {
            if (!value.is_object()) {
                continue;
            }
            CategoryDefinition definition{
                Trim(value.value("id", std::string{})),
                Trim(value.value("name", std::string{})),
            };
            if (!definition.id.empty() && !definition.name.empty() &&
                seen.insert(definition.id).second) {
                config.categoryDefinitions.push_back(std::move(definition));
            }
        }
    }
    if (config.categoryDefinitions.empty()) {
        config.categoryDefinitions = DefaultCategoryDefinitions();
    } else {
        const bool hasOther = std::any_of(
            config.categoryDefinitions.begin(), config.categoryDefinitions.end(),
            [](const CategoryDefinition& definition) { return definition.id == "other"; });
        if (!hasOther) {
            config.categoryDefinitions.push_back({"other", "其他"});
        }
    }

    config.categories = ReadStringMap(json, "categories");

    config.translations.clear();
    if (const auto translations = json.find("translations");
        translations != json.end() && translations->is_object()) {
        for (auto item = translations->begin(); item != translations->end(); ++item) {
            if (!item.value().is_object()) {
                continue;
            }
            TranslationEntry entry;
            entry.sourceHash = item.value().value("sourceHash", "");
            entry.text = item.value().value("text", "");
            entry.updatedAt = item.value().value("updatedAt", "");
            if (!entry.sourceHash.empty() && !entry.text.empty()) {
                config.translations.emplace(item.key(), std::move(entry));
            }
        }
    }

    if (const auto api = json.find("api"); api != json.end() && api->is_object()) {
        config.api.protocol = api->value("protocol", config.api.protocol);
        config.api.baseUrl = api->value("baseUrl", config.api.baseUrl);
        config.api.model = api->value("model", config.api.model);
        config.api.apiKeyCipher = api->value("apiKeyCipher", "");
    }

    if (const auto ui = json.find("ui"); ui != json.end() && ui->is_object()) {
        config.ui.pinned = ui->value("pinned", false);
        config.ui.allCollapsed = ui->value("allCollapsed", false);
        if (const auto collapsed = ui->find("collapsedCategories");
            collapsed != ui->end() && collapsed->is_array()) {
            for (const auto& value : *collapsed) {
                if (value.is_string()) {
                    config.ui.collapsedCategories.push_back(value.get<std::string>());
                }
            }
        }
    }

    return config;
}

ConfigService::ConfigService(std::filesystem::path file) : file_(std::move(file)) {}

const std::filesystem::path& ConfigService::path() const {
    return file_;
}

LoadResult ConfigService::Load() {
    LoadResult result;
    result.config = DefaultConfig();

    if (!std::filesystem::exists(file_)) {
        SaveAtomic(result.config);
        return result;
    }

    const std::string text = ReadTextFile(file_);
    std::string error;
    try {
        const nlohmann::json json = nlohmann::json::parse(text);
        if (const auto parsed = FromJson(json, &error); parsed.has_value()) {
            result.config = *parsed;
            return result;
        }
    } catch (const std::exception& exception) {
        error = exception.what();
    }

    result.recreated = true;
    result.warning = error.empty() ? "invalid config" : error;
    result.backupPath = file_;
    result.backupPath += ".corrupt-" + Timestamp();

    std::error_code copyError;
    std::filesystem::copy_file(
        file_, result.backupPath, std::filesystem::copy_options::overwrite_existing, copyError);
    if (copyError) {
        result.backupPath.clear();
    }
    SaveAtomic(result.config);
    return result;
}

bool ConfigService::Save(const AppConfig& config) const {
    return SaveAtomic(config);
}

bool ConfigService::SaveAtomic(const AppConfig& config) const {
    std::error_code error;
    if (!file_.parent_path().empty()) {
        std::filesystem::create_directories(file_.parent_path(), error);
        if (error) {
            return false;
        }
    }

    std::filesystem::path temporary = file_;
    temporary += L".tmp";

    try {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return false;
        }
        stream << ToJson(config).dump(2) << '\n';
        stream.flush();
        if (!stream) {
            return false;
        }
    } catch (...) {
        std::filesystem::remove(temporary, error);
        return false;
    }

    if (!MoveFileExW(
            temporary.c_str(), file_.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

}  // namespace skillbox

