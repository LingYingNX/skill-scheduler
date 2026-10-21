#include <windows.h>

#include <objbase.h>

#include "app/Application.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE instanceMutex = CreateMutexW(
        nullptr, FALSE, L"Local\\SkillBox.SingleInstance");
    if (!instanceMutex) {
        MessageBoxW(nullptr, L"无法启动 Skill调度器。", L"Skill调度器", MB_OK | MB_ICONERROR);
        return 1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"Skill调度器已在运行。", L"Skill调度器", MB_OK | MB_ICONINFORMATION);
        CloseHandle(instanceMutex);
        return 0;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        MessageBoxW(nullptr, L"COM 初始化失败。", L"Skill调度器", MB_OK | MB_ICONERROR);
        CloseHandle(instanceMutex);
        return 1;
    }

    skillbox::Application application;
    const int result = application.Run(instance);
    CoUninitialize();
    CloseHandle(instanceMutex);
    return result;
}

