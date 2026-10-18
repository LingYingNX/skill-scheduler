#include "platform/Dpapi.h"

#include <windows.h>
#include <wincrypt.h>

#include <vector>

namespace skillbox {
namespace {

std::string EncodeBase64(const BYTE* data, DWORD size) {
    DWORD chars = 0;
    if (!CryptBinaryToStringA(
            data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &chars)) {
        return {};
    }
    std::string result(chars, '\0');
    if (!CryptBinaryToStringA(
            data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, result.data(), &chars)) {
        return {};
    }
    result.resize(chars);
    return result;
}

std::vector<BYTE> DecodeBase64(std::string_view text) {
    DWORD size = 0;
    if (!CryptStringToBinaryA(
            text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64,
            nullptr, &size, nullptr, nullptr)) {
        return {};
    }
    std::vector<BYTE> result(size);
    if (!CryptStringToBinaryA(
            text.data(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64,
            result.data(), &size, nullptr, nullptr)) {
        return {};
    }
    result.resize(size);
    return result;
}

}  // namespace

std::string ProtectSecret(std::string_view plaintext) {
    if (plaintext.empty()) {
        return {};
    }

    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(plaintext.size());
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));

    DATA_BLOB output{};
    if (!CryptProtectData(
            &input, L"SkillBox API key", nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return {};
    }

    std::string encoded = EncodeBase64(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return encoded;
}

std::optional<std::string> UnprotectSecret(std::string_view ciphertext) {
    if (ciphertext.empty()) {
        return std::nullopt;
    }

    std::vector<BYTE> bytes = DecodeBase64(ciphertext);
    if (bytes.empty()) {
        return std::nullopt;
    }

    DATA_BLOB input{};
    input.cbData = static_cast<DWORD>(bytes.size());
    input.pbData = bytes.data();

    DATA_BLOB output{};
    if (!CryptUnprotectData(
            &input, nullptr, nullptr, nullptr, nullptr,
            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return std::nullopt;
    }

    std::string result(reinterpret_cast<char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    return result;
}

}  // namespace skillbox

