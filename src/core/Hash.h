#pragma once

#include <string>
#include <string_view>

namespace skillbox {

std::string Sha256Hex(std::string_view text);

}  // namespace skillbox

