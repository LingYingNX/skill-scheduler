#include "core/SkillCatalog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <set>
#include <system_error>

#include "core/PathUtils.h"
#include "core/SkillParser.h"

namespace skillbox {
namespace {

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
    const std::vector<CategoryDefinition>& definitions) {
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

        std::filesystem::recursive_directory_iterator iterator(
            rootPath, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        for (; !error && iterator != end; iterator.increment(error)) {
            if (iterator->is_symlink(error)) {
                if (iterator->is_directory(error)) {
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

            const std::string normalized = NormalizePath(iterator->path());
            const std::string key = PathKey(normalized);
            if (normalized.empty() || seen.contains(key)) {
                continue;
            }
            seen.insert(key);

            const ParsedSkill parsed = ParseSkillFile(iterator->path());
            if (!parsed.ok) {
                continue;
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
        }
    }

    std::sort(snapshot.skills.begin(), snapshot.skills.end(), [&](const SkillEntry& left, const SkillEntry& right) {
        const int leftRank = CategoryRank(definitions, left.category);
        const int rightRank = CategoryRank(definitions, right.category);
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

void ReconcileCatalogConfig(AppConfig& config, const CatalogSnapshot& snapshot) {
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
}

}  // namespace skillbox

