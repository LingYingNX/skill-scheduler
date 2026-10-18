#include "platform/Clipboard.h"

#include <windows.h>

#include <cstring>

#include "core/PathUtils.h"

namespace skillbox {
namespace {

bool OpenClipboardWithRetry() {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(nullptr)) {
            return true;
        }
        Sleep(10);
    }
    return false;
}

}  // namespace

bool SetClipboardText(std::string_view utf8Text) {
    const std::wstring text = Utf8ToWide(utf8Text);
    if (!OpenClipboardWithRetry()) {
        return false;
    }
    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        CloseClipboard();
        return false;
    }
    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    std::memcpy(target, text.c_str(), bytes);
    GlobalUnlock(memory);

    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

std::optional<std::string> GetClipboardText() {
    if (!OpenClipboardWithRetry()) {
        return std::nullopt;
    }
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data) {
        CloseClipboard();
        return std::nullopt;
    }
    const auto* text = static_cast<const wchar_t*>(GlobalLock(data));
    if (!text) {
        CloseClipboard();
        return std::nullopt;
    }
    const std::string result = WideToUtf8(text);
    GlobalUnlock(data);
    CloseClipboard();
    return result;
}

}  // namespace skillbox

