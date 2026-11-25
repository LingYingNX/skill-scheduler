#include "core/SkillCatalog.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <map>
#include <set>
#include <system_error>

#include "core/PathUtils.h"
#include "core/SkillParser.h"

namespace skillbox {
namespace {

bool IsReparseDirectory(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

int CategoryRank(
    const std::vector<CategoryDefinition>& definitions,
    const std::string& category) {
    const auto found = std::find_if(
        definitions.begin(), definitions.end(),
        [&](const CategoryDefinition& definition) { return definition.id == category; });
    return found == definitions.end()
               ? static_cast<int>(definitions.size())
               : static_cast<int>(std::distance(definitions.begin(), found));
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string MakeSignature(const CatalogSnapshot& snapshot) {
    std::string signature;
    for (const RootStatus& root : snapshot.roots) {
        signature += "R|" + PathKey(root.path) + "|" +
                     (root.available ? "1" : "0") + "\n";
    }
    for (const SkillEntry& skill : snapshot.skills) {
        signature += "S|" + PathKey(skill.path) + "|" + skill.name + "|" +
                     skill.description + "|" + skill.category + "|" + skill.warning + "\n";
    }
    return signature;
}

}  // namespace

const CategoryDefinition* FindCategory(
    const AppConfig& config,
    std::string_view categoryId) {
    const auto found = std::find_if(
        config.categoryDefinitions.begin(), config.categoryDefinitions.end(),
        [&](const CategoryDefinition& definition) { return definition.id == categoryId; });
    return found == config.categoryDefinitions.end() ? nullptr : &*found;
}

CatalogSnapshot ScanSkills(
    const std::vector<std::string>& roots,
    const std::unordered_map<std::string, std::string>& categories,
    const std::vector<CategoryDefinition>& definitions,
    const std::vector<std::string>& skillOrder) {
    CatalogSnapshot snapshot;
    std::set<std::string> seen;
    const std::vector<std::string> normalizedRoots = DeduplicatePaths(roots);
    std::unordered_map<std::string, std::string> categoryByKey;
    for (const auto& [path, category] : categories) {
        categoryByKey[PathKey(path)] = category;
    }

    for (const std::string& rootText : normalizedRoots) {
        RootStatus root;
        root.path = rootText;
        const std::filesystem::path rootPath = Utf8ToWide(rootText);

        std::error_code error;
        root.available = std::filesystem::is_directory(rootPath, error);
        if (!root.available) {
            root.error = error ? error.message() : "path is unavailable";
            snapshot.roots.push_back(std::move(root));
            continue;
        }
        snapshot.roots.push_back(root);

        const auto loadSkillFile = [&](const std::filesystem::path& skillFile) {
            const std::string normalized = NormalizePath(skillFile);
            const std::string key = PathKey(normalized);
            if (normalized.empty() || seen.contains(key)) {
                return;
            }
            seen.insert(key);

            const ParsedSkill parsed = ParseSkillFile(skillFile);
            if (!parsed.ok) {
                return;
            }

            SkillEntry skill;
            skill.path = normalized;
            skill.root = rootText;
            skill.name = parsed.name;
            skill.command = "/" + parsed.name;
            skill.description = parsed.description;
            skill.warning = parsed.warning;
            if (const auto found = categoryByKey.find(key); found != categoryByKey.end()) {
                const bool valid = std::any_of(
                    definitions.begin(), definitions.end(),
                    [&](const CategoryDefinition& definition) {
                        return definition.id == found->second;
                    });
                if (valid) {
                    skill.category = found->second;
                }
            }
            snapshot.skills.push_back(std::move(skill));
        };

        std::filesystem::recursive_directory_iterator iterator(
            rootPath, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        for (; !error && iterator != end; iterator.increment(error)) {
            const bool symlink = iterator->is_symlink(error);
            const bool linkedDirectory =
                IsReparseDirectory(iterator->path()) ||
                (symlink && iterator->is_directory(error));
            if (symlink || linkedDirectory) {
                if (linkedDirectory) {
                    const std::filesystem::path linkedSkill = iterator->path() / L"SKILL.md";
                    std::error_code linkedError;
                    if (std::filesystem::is_regular_file(linkedSkill, linkedError) &&
                        !linkedError) {
                        loadSkillFile(linkedSkill);
                    }
                    iterator.disable_recursion_pending();
                }
                error.clear();
                continue;
            }
            if (iterator.depth() >= kMaxSkillDepth && iterator->is_directory(error)) {
                iterator.disable_recursion_pending();
            }
            if (!iterator->is_regular_file(error)) {
                continue;
            }
            if (LowerAscii(iterator->path().filename().string()) != "skill.md") {
                continue;
            }
            loadSkillFile(iterator->path());
        }
        if (error) {
            snapshot.complete = false;
        }
    }

    std::unordered_map<std::string, std::size_t> orderRanks;
    for (std::size_t index = 0; index < skillOrder.size(); ++index) {
        orderRanks.emplace(PathKey(skillOrder[index]), index);
    }
    const std::size_t unorderedRank = std::numeric_limits<std::size_t>::max();
    std::sort(
        snapshot.skills.begin(),
        snapshot.skills.end(),
        [&](const SkillEntry& left, const SkillEntry& right) {
            const int leftCategoryRank = CategoryRank(definitions, left.category);
            const int rightCategoryRank = CategoryRank(definitions, right.category);
            if (leftCategoryRank != rightCategoryRank) {
                return leftCategoryRank < rightCategoryRank;
            }
            const auto leftOrder = orderRanks.find(PathKey(left.path));
            const auto rightOrder = orderRanks.find(PathKey(right.path));
            const std::size_t leftRank =
                leftOrder == orderRanks.end() ? unorderedRank : leftOrder->second;
            const std::size_t rightRank =
                rightOrder == orderRanks.end() ? unorderedRank : rightOrder->second;
            if (leftRank != rightRank) {
                return leftRank < rightRank;
            }
            const std::string leftName = LowerAscii(left.name);
            const std::string rightName = LowerAscii(right.name);
            if (leftName != rightName) {
                return leftName < rightName;
            }
            return PathKey(left.path) < PathKey(right.path);
        });

    snapshot.signature = MakeSignature(snapshot);
    return snapshot;
}

bool SetSkillCategory(
    AppConfig& config,
    const std::string& path,
    const std::string& category) {
    if (!FindCategory(config, category)) {
        return false;
    }
    const std::string normalized = NormalizePathUtf8(path);
    if (normalized.empty()) {
        return false;
    }
    config.categories[normalized] = category;
    return true;
}

bool ReorderSkill(
    AppConfig& config,
    const CatalogSnapshot& snapshot,
    const std::string& path,
    const std::string& targetPath,
    bool after) {
    if (path.empty() || targetPath.empty() || PathKey(path) == PathKey(targetPath)) {
        return false;
    }
    const auto moving = std::find_if(
        snapshot.skills.begin(), snapshot.skills.end(),
        [&](const SkillEntry& skill) { return skill.path == path; });
    const auto target = std::find_if(
        snapshot.skills.begin(), snapshot.skills.end(),
        [&](const SkillEntry& skill) { return skill.path == targetPath; });
    if (moving == snapshot.skills.end() || target == snapshot.skills.end() ||
        moving->category != target->category) {
        return false;
    }

    std::vector<std::string> orderedPaths;
    orderedPaths.reserve(snapshot.skills.size());
    for (const SkillEntry& skill : snapshot.skills) {
        orderedPaths.push_back(skill.path);
    }
    orderedPaths.erase(
        std::remove(orderedPaths.begin(), orderedPaths.end(), path),
        orderedPaths.end());
    const auto targetPosition =
        std::find(orderedPaths.begin(), orderedPaths.end(), targetPath);
    if (targetPosition == orderedPaths.end()) {
        return false;
    }
    orderedPaths.insert(targetPosition + (after ? 1 : 0), path);
    config.skillOrder = std::move(orderedPaths);
    return true;
}

bool RenameCategory(
    AppConfig& config,
    const std::string& id,
    const std::string& name) {
    const std::string trimmed = Trim(name);
    if (trimmed.empty() || trimmed.size() > 24) {
        return false;
    }
    const bool duplicate = std::any_of(
        config.categoryDefinitions.begin(), config.categoryDefinitions.end(),
        [&](const CategoryDefinition& definition) {
            return definition.id != id && definition.name == trimmed;
        });
    if (duplicate) {
        return false;
    }
    const auto found = std::find_if(
        config.categoryDefinitions.begin(), config.categoryDefinitions.end(),
        [&](const CategoryDefinition& definition) { return definition.id == id; });
    if (found == config.categoryDefinitions.end()) {
        return false;
    }
    found->name = trimmed;
    return true;
}

bool AddCategory(AppConfig& config, const std::string& name, std::string* newId) {
    const std::string trimmed = Trim(name);
    if (trimmed.empty() || trimmed.size() > 24) {
        return false;
    }
    const bool duplicate = std::any_of(
        config.categoryDefinitions.begin(), config.categoryDefinitions.end(),
        [&](const CategoryDefinition& definition) { return definition.name == trimmed; });
    if (duplicate) {
        return false;
    }

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string id = "custom-" + std::to_string(stamp);
    while (FindCategory(config, id)) {
        id.push_back('x');
    }
    config.categoryDefinitions.push_back({id, trimmed});
    if (newId) {
        *newId = id;
    }
    return true;
}

bool DeleteCategory(AppConfig& config, const std::string& id) {
    if (id == "other") {
        return false;
    }
    const auto found = std::find_if(
        config.categoryDefinitions.begin(), config.categoryDefinitions.end(),
        [&](const CategoryDefinition& definition) { return definition.id == id; });
    if (found == config.categoryDefinitions.end()) {
        return false;
    }
    config.categoryDefinitions.erase(found);
    config.ui.collapsedCategories.erase(
        std::remove(
            config.ui.collapsedCategories.begin(),
            config.ui.collapsedCategories.end(),
            id),
        config.ui.collapsedCategories.end());
    for (auto iterator = config.categories.begin(); iterator != config.categories.end();) {
        iterator = iterator->second == id ? config.categories.erase(iterator)
                                          : std::next(iterator);
    }
    return true;
}

bool ReorderCategories(
    AppConfig& config,
    const std::vector<std::string>& orderedIds) {
    if (orderedIds.size() != config.categoryDefinitions.size()) {
        return false;
    }
    std::vector<CategoryDefinition> reordered;
    reordered.reserve(orderedIds.size());
    std::set<std::string> seen;
    for (const std::string& id : orderedIds) {
        const auto found = std::find_if(
            config.categoryDefinitions.begin(), config.categoryDefinitions.end(),
            [&](const CategoryDefinition& definition) { return definition.id == id; });
        if (found == config.categoryDefinitions.end() || !seen.insert(id).second) {
            return false;
        }
        reordered.push_back(*found);
    }
    config.categoryDefinitions = std::move(reordered);
    return true;
}

void ReconcileCatalogConfig(AppConfig& config, const CatalogSnapshot& snapshot) {
    if (!snapshot.complete) {
        return;
    }
    std::set<std::string> discovered;
    for (const SkillEntry& skill : snapshot.skills) {
        discovered.insert(PathKey(skill.path));
    }

    auto shouldKeep = [&](const std::string& path) {
        if (discovered.contains(PathKey(path))) {
            return true;
        }
        for (const RootStatus& root : snapshot.roots) {
            if (!root.available && IsPathWithin(root.path, path)) {
                return true;
            }
        }
        return false;
    };

    for (auto iterator = config.categories.begin(); iterator != config.categories.end();) {
        iterator = shouldKeep(iterator->first) ? std::next(iterator)
                                               : config.categories.erase(iterator);
    }
    for (auto iterator = config.translations.begin(); iterator != config.translations.end();) {
        iterator = shouldKeep(iterator->first) ? std::next(iterator)
                                               : config.translations.erase(iterator);
    }
    for (auto iterator = config.translationsEn.begin(); iterator != config.translationsEn.end();) {
        iterator = shouldKeep(iterator->first) ? std::next(iterator)
                                               : config.translationsEn.erase(iterator);
    }
    for (auto iterator = config.notes.begin(); iterator != config.notes.end();) {
        iterator = shouldKeep(iterator->first) ? std::next(iterator)
                                               : config.notes.erase(iterator);
    }
    std::erase_if(
        config.skillOrder,
        [&](const std::string& path) { return !shouldKeep(path); });
}

}  // namespace skillbox

