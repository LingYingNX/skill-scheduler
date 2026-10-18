#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace skillbox {

std::string ProtectSecret(std::string_view plaintext);
std::optional<std::string> UnprotectSecret(std::string_view ciphertext);

}  // namespace skillbox

