#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace skillbox {

std::string WideToUtf8(std::wstring_view value);
std::wstring Utf8ToWide(std::string_view value);

std::string NormalizePath(const std::filesystem::path& path);
std::string NormalizePathUtf8(std::string_view path);
std::string PathKey(std::string_view path);
bool SamePath(std::string_view left, std::string_view right);
bool IsPathWithin(std::string_view parent, std::string_view child);
std::vector<std::string> DeduplicatePaths(const std::vector<std::string>& paths);
std::string DefaultSkillsPath();

}  // namespace skillbox

