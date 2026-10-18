#include <windows.h>

#include <objbase.h>

#include "app/Application.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        MessageBoxW(nullptr, L"COM initialization failed.", L"SkillBox", MB_OK | MB_ICONERROR);
        return 1;
    }

    skillbox::Application application;
    const int result = application.Run(instance);
    CoUninitialize();
    return result;
}

