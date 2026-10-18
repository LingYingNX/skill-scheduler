#pragma once

#include <filesystem>
#include <string>

namespace skillbox {

struct ParsedSkill {
    bool ok = false;
    std::string name;
    std::string description;
    std::string warning;
    std::string error;
};

ParsedSkill ParseSkillFile(const std::filesystem::path& skillFile);

}  // namespace skillbox

