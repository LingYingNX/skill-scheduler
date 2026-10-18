#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace skillbox {

bool SetClipboardText(std::string_view utf8Text);
std::optional<std::string> GetClipboardText();

}  // namespace skillbox

