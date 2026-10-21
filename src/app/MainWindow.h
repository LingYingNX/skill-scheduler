#pragma once

#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

namespace skillbox {

class MainWindow {
public:
    using MessageHandler = std::function<std::string(const std::string&)>;

    explicit MainWindow(MessageHandler handler);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool Create(void* instance, bool pinned);
    int Run();

    void SetPinned(bool pinned);
    bool IsPinned() const;
    void ToggleMaximize();
    void Minimize();
    void Drag();
    void StartResize(const std::string& direction);
    void Close();
    void PostEvent(const std::string& type, nlohmann::json data = {});
    void PostRawMessage(std::string message);
    void* NativeHandle() const;
    bool ExecuteScript(const std::wstring& script);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace skillbox

