#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/Config.h"

namespace skillbox {

struct SkillEntry {
    std::string path;
    std::string root;
    std::string name;
    std::string command;
    std::string description;
    std::string category = "other";
    std::string warning;
};

struct RootStatus {
    std::string path;
    bool available = false;
    std::string error;
};

struct CatalogSnapshot {
    std::vector<SkillEntry> skills;
    std::vector<RootStatus> roots;
    std::string signature;
};

inline constexpr int kMaxSkillDepth = 16;

const CategoryDefinition* FindCategory(
    const AppConfig& config,
    std::string_view categoryId);
CatalogSnapshot ScanSkills(
    const std::vector<std::string>& roots,
    const std::unordered_map<std::string, std::string>& categories,
    const std::vector<CategoryDefinition>& definitions);
bool SetSkillCategory(AppConfig& config, const std::string& path, const std::string& category);
bool RenameCategory(AppConfig& config, const std::string& id, const std::string& name);
bool AddCategory(AppConfig& config, const std::string& name, std::string* newId = nullptr);
void ReconcileCatalogConfig(AppConfig& config, const CatalogSnapshot& snapshot);

}  // namespace skillbox

