#include "app/MainWindow.h"

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <WebView2.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "core/PathUtils.h"

namespace skillbox {
namespace {

constexpr wchar_t kWindowClass[] = L"SkillBox.MainWindow";
constexpr UINT kCatalogChangedMessage = WM_APP + 11;
constexpr UINT kPostedEventMessage = WM_APP + 12;
constexpr int kBaseWidth = 420;
constexpr int kBaseHeight = 680;
constexpr int kBaseMinimumWidth = 380;
constexpr int kBaseMinimumHeight = 560;

std::filesystem::path ExecutableDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD size = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(size);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path WebViewDataDirectory() {
    DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (size == 0) {
        return ExecutableDirectory() / L"WebView2";
    }
    std::wstring value(size, L'\0');
    const DWORD written =
        GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), size);
    value.resize(written);
    return std::filesystem::path(value) / L"SkillBox" / L"WebView2";
}

}  // namespace

struct MainWindow::Impl {
    MessageHandler messageHandler;
    HWND hwnd = nullptr;
    HINSTANCE instance = nullptr;
    UINT dpi = 96;
    bool pinned = false;
    bool webViewReady = false;
    std::mutex postMutex;
    std::vector<std::string> postedEvents;
    EventRegistrationToken messageToken{};
    Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webView;

    int Scale(int value) const {
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    void ResizeWebView() const {
        if (!controller || !hwnd) {
            return;
        }
        RECT bounds{};
        GetClientRect(hwnd, &bounds);
        controller->put_Bounds(bounds);
    }

    void ApplyPinned() {
        if (!hwnd) {
            return;
        }
        SetWindowPos(
            hwnd,
            pinned ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void InitializeWebView() {
        const std::filesystem::path dataDirectory = WebViewDataDirectory();
        std::error_code error;
        std::filesystem::create_directories(dataDirectory, error);

        const auto environmentHandler =
            Microsoft::WRL::Callback<
                ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this](HRESULT result, ICoreWebView2Environment* createdEnvironment) -> HRESULT {
                    if (FAILED(result) || !createdEnvironment) {
                        MessageBoxW(
                            hwnd,
                            L"WebView2 Runtime is unavailable. Install the Evergreen Runtime and try again.",
                            L"SkillBox",
                            MB_OK | MB_ICONERROR);
                        PostQuitMessage(1);
                        return S_OK;
                    }
                    environment = createdEnvironment;

                    const auto controllerHandler =
                        Microsoft::WRL::Callback<
                            ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this](HRESULT controllerResult, ICoreWebView2Controller* createdController) -> HRESULT {
                                if (FAILED(controllerResult) || !createdController) {
                                    PostQuitMessage(1);
                                    return S_OK;
                                }
                                controller = createdController;
                                controller->get_CoreWebView2(&webView);
                                ResizeWebView();

                                Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                                if (SUCCEEDED(webView->get_Settings(&settings)) && settings) {
                                    settings->put_AreDefaultContextMenusEnabled(FALSE);
                                    settings->put_IsStatusBarEnabled(FALSE);
                                }

                                const auto messageHandler =
                                    Microsoft::WRL::Callback<
                                        ICoreWebView2WebMessageReceivedEventHandler>(
                                        [this](
                                            ICoreWebView2*,
                                            ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                            LPWSTR json = nullptr;
                                            if (FAILED(args->get_WebMessageAsJson(&json)) || !json) {
                                                return S_OK;
                                            }
                                            const std::string request = WideToUtf8(json);
                                            CoTaskMemFree(json);
                                            if (this->messageHandler) {
                                                const std::string response = this->messageHandler(request);
                                                const std::wstring wide = Utf8ToWide(response);
                                                webView->PostWebMessageAsJson(wide.c_str());
                                            }
                                            return S_OK;
                                        });
                                webView->add_WebMessageReceived(messageHandler.Get(), &messageToken);
                                {
                                    std::scoped_lock lock(postMutex);
                                    for (const std::string& event : postedEvents) {
                                        const std::wstring wide = Utf8ToWide(event);
                                        webView->PostWebMessageAsJson(wide.c_str());
                                    }
                                    postedEvents.clear();
                                    webViewReady = true;
                                }

                                Microsoft::WRL::ComPtr<ICoreWebView2_3> webView3;
                                if (SUCCEEDED(webView.As(&webView3)) && webView3) {
                                    const std::filesystem::path webRoot =
                                        ExecutableDirectory() / L"web";
                                    webView3->SetVirtualHostNameToFolderMapping(
                                        L"skillbox.local",
                                        webRoot.c_str(),
                                        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                                }

                                webView->Navigate(L"https://skillbox.local/index.html");
                                controller->put_IsVisible(TRUE);
                                return S_OK;
                            });
                    environment->CreateCoreWebView2Controller(hwnd, controllerHandler.Get());
                    return S_OK;
                });

        const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(
            nullptr, dataDirectory.c_str(), nullptr, environmentHandler.Get());
        if (FAILED(result)) {
            MessageBoxW(
                hwnd,
                L"Unable to start WebView2.",
                L"SkillBox",
                MB_OK | MB_ICONERROR);
        }
    }

    static LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) {
        Impl* self = reinterpret_cast<Impl*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create =
                reinterpret_cast<const CREATESTRUCTW*>(lParam);
            self = static_cast<Impl*>(create->lpCreateParams);
            self->hwnd = hwnd;
            SetWindowLongPtrW(
                hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            SetWindowLongPtrW(
                hwnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(&Impl::WindowProc));
        }
        return self ? self->HandleMessage(message, wParam, lParam)
                    : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_NCCALCSIZE:
                if (wParam) {
                    return 0;
                }
                break;
            case WM_NCHITTEST: {
                const LRESULT base = DefWindowProcW(hwnd, message, wParam, lParam);
                if (base != HTCLIENT) {
                    return base;
                }
                POINT cursor{
                    GET_X_LPARAM(lParam),
                    GET_Y_LPARAM(lParam)};
                RECT window{};
                GetWindowRect(hwnd, &window);
                const int border = Scale(7);
                const bool left = cursor.x < window.left + border;
                const bool right = cursor.x >= window.right - border;
                const bool top = cursor.y < window.top + border;
                const bool bottom = cursor.y >= window.bottom - border;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
                return HTCLIENT;
            }
            case WM_GETMINMAXINFO: {
                auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
                info->ptMinTrackSize.x = Scale(kBaseMinimumWidth);
                info->ptMinTrackSize.y = Scale(kBaseMinimumHeight);
                return 0;
            }
            case WM_SIZE:
                ResizeWebView();
                return 0;
            case WM_DPICHANGED: {
                dpi = HIWORD(wParam);
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(
                    hwnd,
                    nullptr,
                    suggested->left,
                    suggested->top,
                    suggested->right - suggested->left,
                    suggested->bottom - suggested->top,
                    SWP_NOZORDER | SWP_NOACTIVATE);
                ResizeWebView();
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            case kCatalogChangedMessage:
                if (webViewReady) {
                    webView->PostWebMessageAsJson(L"{\"type\":\"catalogChanged\"}");
                }
                return 0;
            case kPostedEventMessage: {
                std::unique_ptr<std::string> payload(
                    reinterpret_cast<std::string*>(lParam));
                if (webViewReady && payload) {
                    const std::wstring wide = Utf8ToWide(*payload);
                    webView->PostWebMessageAsJson(wide.c_str());
                }
                return 0;
            }
            default:
                break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
};

MainWindow::MainWindow(MessageHandler handler) : impl_(std::make_unique<Impl>()) {
    impl_->messageHandler = std::move(handler);
}

MainWindow::~MainWindow() {
    if (impl_ && impl_->webView && impl_->webViewReady) {
        impl_->webView->remove_WebMessageReceived(impl_->messageToken);
    }
}

bool MainWindow::Create(void* instance, bool pinned) {
    impl_->instance = static_cast<HINSTANCE>(instance);
    impl_->pinned = pinned;
    impl_->dpi = GetDpiForSystem();

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = impl_->instance;
    windowClass.lpfnWndProc = &Impl::WindowProc;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    if (!RegisterClassExW(&windowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    const int width = impl_->Scale(kBaseWidth);
    const int height = impl_->Scale(kBaseHeight);
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    impl_->hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        kWindowClass,
        L"SkillBox",
        WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
        (screenWidth - width) / 2,
        (screenHeight - height) / 2,
        width,
        height,
        nullptr,
        nullptr,
        impl_->instance,
        impl_.get());
    if (!impl_->hwnd) {
        return false;
    }

    const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(
        impl_->hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
        &preference, sizeof(preference));
    MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(impl_->hwnd, &margins);

    ShowWindow(impl_->hwnd, SW_SHOW);
    UpdateWindow(impl_->hwnd);
    impl_->ApplyPinned();
    impl_->InitializeWebView();
    return true;
}

int MainWindow::Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void MainWindow::SetPinned(bool pinned) {
    impl_->pinned = pinned;
    impl_->ApplyPinned();
}

bool MainWindow::IsPinned() const {
    return impl_->pinned;
}

void MainWindow::ToggleMaximize() {
    ShowWindow(impl_->hwnd, IsZoomed(impl_->hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

void MainWindow::Minimize() {
    ShowWindow(impl_->hwnd, SW_MINIMIZE);
}

void MainWindow::Drag() {
    ReleaseCapture();
    SendMessageW(impl_->hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void MainWindow::StartResize(const std::string& direction) {
    WPARAM hit = HTBOTTOMRIGHT;
    if (direction == "left") hit = HTLEFT;
    else if (direction == "right") hit = HTRIGHT;
    else if (direction == "top") hit = HTTOP;
    else if (direction == "bottom") hit = HTBOTTOM;
    else if (direction == "top-left") hit = HTTOPLEFT;
    else if (direction == "top-right") hit = HTTOPRIGHT;
    else if (direction == "bottom-left") hit = HTBOTTOMLEFT;
    ReleaseCapture();
    SendMessageW(impl_->hwnd, WM_NCLBUTTONDOWN, hit, 0);
}

void MainWindow::Close() {
    PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0);
}

void MainWindow::PostEvent(const std::string& type, nlohmann::json data) {
    if (!impl_->hwnd) {
        return;
    }
    if (data.is_null()) {
        data = nlohmann::json::object();
    }
    const std::string event = nlohmann::json{{"type", type}, {"data", std::move(data)}}.dump();
    {
        std::scoped_lock lock(impl_->postMutex);
        if (impl_->webViewReady && impl_->webView) {
            const std::wstring wide = Utf8ToWide(event);
            impl_->webView->PostWebMessageAsJson(wide.c_str());
            return;
        }
        impl_->postedEvents.push_back(event);
        auto* payload = new std::string(event);
        PostMessageW(
            impl_->hwnd,
            kPostedEventMessage,
            0,
            reinterpret_cast<LPARAM>(payload));
    }
}

void* MainWindow::NativeHandle() const {
    return impl_->hwnd;
}

bool MainWindow::ExecuteScript(const std::wstring& script) {
    if (!impl_->webViewReady || !impl_->webView) {
        return false;
    }
    const auto handler = Microsoft::WRL::Callback<
        ICoreWebView2ExecuteScriptCompletedHandler>(
        [](HRESULT, LPCWSTR) -> HRESULT {
            return S_OK;
        });
    return SUCCEEDED(impl_->webView->ExecuteScript(script.c_str(), handler.Get()));
}

}  // namespace skillbox
