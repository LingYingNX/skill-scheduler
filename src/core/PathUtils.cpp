#include "core/PathUtils.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <system_error>

namespace skillbox {

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (size <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), size);
    return result;
}

std::string NormalizePath(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }

    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    if (error) {
        return {};
    }

    std::filesystem::path normalized = std::filesystem::weakly_canonical(absolute, error);
    if (error) {
        normalized = absolute.lexically_normal();
    }

    std::wstring text = normalized.wstring();
    while (text.size() > 1 && (text.back() == L'\\' || text.back() == L'/')) {
        text.pop_back();
    }
    return WideToUtf8(text);
}

std::string NormalizePathUtf8(std::string_view path) {
    const std::wstring wide = Utf8ToWide(path);
    if (wide.empty()) {
        return {};
    }
    return NormalizePath(std::filesystem::path(wide));
}

std::string PathKey(std::string_view path) {
    std::wstring key = Utf8ToWide(path);
    if (key.empty()) {
        return {};
    }
    std::replace(key.begin(), key.end(), L'/', L'\\');
    CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
    return WideToUtf8(key);
}

bool SamePath(std::string_view left, std::string_view right) {
    return PathKey(left) == PathKey(right);
}

bool IsPathWithin(std::string_view parent, std::string_view child) {
    const std::wstring parentKey = Utf8ToWide(PathKey(parent));
    const std::wstring childKey = Utf8ToWide(PathKey(child));
    if (parentKey.empty() || childKey.empty() || parentKey.size() > childKey.size()) {
        return false;
    }
    if (childKey.compare(0, parentKey.size(), parentKey) != 0) {
        return false;
    }
    return childKey.size() == parentKey.size() ||
           childKey[parentKey.size()] == L'\\' ||
           parentKey.back() == L'\\';
}

std::vector<std::string> DeduplicatePaths(const std::vector<std::string>& paths) {
    std::vector<std::string> result;
    for (const std::string& raw : paths) {
        const std::string normalized = NormalizePathUtf8(raw);
        if (normalized.empty()) {
            continue;
        }
        const bool exists = std::any_of(
            result.begin(), result.end(),
            [&](const std::string& value) { return SamePath(value, normalized); });
        if (!exists) {
            result.push_back(normalized);
        }
    }
    return result;
}

std::string DefaultSkillsPath() {
    DWORD size = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (size == 0) {
        return {};
    }
    std::wstring profile(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(L"USERPROFILE", profile.data(), size);
    if (written == 0 || written >= size) {
        return {};
    }
    profile.resize(written);
    return NormalizePath(std::filesystem::path(profile) / L".agents" / L"skills");
}

}  // namespace skillbox

