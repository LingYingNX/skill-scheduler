#include "platform/FolderPicker.h"

#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include "core/PathUtils.h"

namespace skillbox {

std::optional<std::string> PickFolder(void* owner) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(
            CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }

    FILEOPENDIALOGOPTIONS options{};
    if (FAILED(dialog->GetOptions(&options))) {
        return std::nullopt;
    }
    options |= FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
    if (FAILED(dialog->SetOptions(options))) {
        return std::nullopt;
    }
    dialog->SetTitle(L"选择 Skills 文件夹");

    if (FAILED(dialog->Show(static_cast<HWND>(owner)))) {
        return std::nullopt;
    }

    Microsoft::WRL::ComPtr<IShellItem> result;
    if (FAILED(dialog->GetResult(&result))) {
        return std::nullopt;
    }
    PWSTR path = nullptr;
    if (FAILED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) {
        return std::nullopt;
    }
    std::string value = NormalizePath(std::filesystem::path(path));
    CoTaskMemFree(path);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

}  // namespace skillbox
