#include "core/Hash.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <iomanip>
#include <sstream>
#include <vector>

namespace skillbox {

std::string Sha256Hex(std::string_view text) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        return {};
    }

    DWORD objectSize = 0;
    DWORD resultSize = 0;
    if (BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize),
            &resultSize, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    std::vector<UCHAR> object(objectSize);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(
            algorithm, &hash, object.data(), objectSize,
            nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    const PUCHAR data = reinterpret_cast<PUCHAR>(const_cast<char*>(text.data()));
    if (BCryptHashData(hash, data, static_cast<ULONG>(text.size()), 0) < 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    std::array<UCHAR, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (UCHAR value : digest) {
        output << std::setw(2) << static_cast<unsigned int>(value);
    }
    return output.str();
}

}  // namespace skillbox

