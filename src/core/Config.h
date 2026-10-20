#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace skillbox {

inline constexpr int kConfigVersion = 1;

struct ApiConfig {
    std::string protocol = "openai";
    std::string baseUrl = "https://api.openai.com/v1";
    std::string model = "gpt-4o-mini";
    std::string apiKeyCipher;
    std::string name;
};

struct TranslationEntry {
    std::string sourceHash;
    std::string text;
    std::string updatedAt;
};

struct CategoryDefinition {
    std::string id;
    std::string name;
};

inline void to_json(nlohmann::json& json, const CategoryDefinition& definition) {
    json = {{"id", definition.id}, {"name", definition.name}};
}

inline void from_json(const nlohmann::json& json, CategoryDefinition& definition) {
    definition.id = json.value("id", std::string{});
    definition.name = json.value("name", std::string{});
}

struct UiConfig {
    bool pinned = false;
    bool allCollapsed = false;
    std::vector<std::string> collapsedCategories;
    std::string theme = "light";
};

struct AppConfig {
    int version = kConfigVersion;
    std::vector<std::string> paths;
    std::vector<CategoryDefinition> categoryDefinitions;
    std::unordered_map<std::string, std::string> categories;
    std::unordered_map<std::string, TranslationEntry> translations;
    std::unordered_map<std::string, ApiConfig> providers;
    std::vector<std::string> enabledProviderIds;
    UiConfig ui;
};

std::vector<CategoryDefinition> DefaultCategoryDefinitions();
std::unordered_map<std::string, ApiConfig> DefaultApiProviders();
ApiConfig* FindApiProvider(AppConfig& config, const std::string& id);
const ApiConfig* FindApiProvider(const AppConfig& config, const std::string& id);
AppConfig DefaultConfig();
nlohmann::json ToJson(const AppConfig& config);
std::optional<AppConfig> FromJson(
    const nlohmann::json& json,
    std::string* error = nullptr);

struct LoadResult {
    AppConfig config;
    bool recreated = false;
    std::filesystem::path backupPath;
    std::string warning;
};

class ConfigService {
public:
    explicit ConfigService(std::filesystem::path file);

    const std::filesystem::path& path() const;
    LoadResult Load();
    bool Save(const AppConfig& config) const;
    bool SaveAtomic(const AppConfig& config) const;

private:
    std::filesystem::path file_;
};

}  // namespace skillbox

