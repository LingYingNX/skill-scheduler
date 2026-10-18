#include "core/SkillParser.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4251 4275)
#endif
#include <yaml-cpp/yaml.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <fstream>
#include <sstream>

#include "core/PathUtils.h"

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

bool IsDelimiter(const std::string& line) {
    return Trim(line) == "---";
}

}  // namespace

ParsedSkill ParseSkillFile(const std::filesystem::path& skillFile) {
    ParsedSkill result;
    std::ifstream stream(skillFile, std::ios::binary);
    if (!stream) {
        result.error = "cannot open SKILL.md";
        return result;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    std::string content = buffer.str();
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
    }

    std::istringstream lines(content);
    std::string line;
    if (!std::getline(lines, line) || !IsDelimiter(line)) {
        result.error = "missing YAML frontmatter";
        return result;
    }

    std::ostringstream frontmatter;
    bool closed = false;
    while (std::getline(lines, line)) {
        if (IsDelimiter(line)) {
            closed = true;
            break;
        }
        frontmatter << line << '\n';
    }
    if (!closed) {
        result.error = "unterminated YAML frontmatter";
        return result;
    }

    try {
        const YAML::Node node = YAML::Load(frontmatter.str());
        if (!node.IsMap()) {
            result.error = "frontmatter is not a map";
            return result;
        }

        if (node["name"] && node["name"].IsScalar()) {
            result.name = Trim(node["name"].as<std::string>());
        }
        if (result.name.empty()) {
            result.name = WideToUtf8(skillFile.parent_path().filename().wstring());
            result.warning = "missing skill name; directory name used";
        }

        if (node["description"] && node["description"].IsScalar()) {
            result.description = Trim(node["description"].as<std::string>());
        }
        if (result.description.empty()) {
            result.warning += result.warning.empty()
                                  ? "missing skill description"
                                  : "; missing skill description";
        }

        result.ok = true;
    } catch (const YAML::Exception& exception) {
        result.error = exception.what();
    }
    return result;
}

}  // namespace skillbox

