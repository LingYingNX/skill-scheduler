# SkillBox

SkillBox 是一个原生 Windows Skills 浏览器与快捷调用工具。

## 架构

- C++20 原生宿主，负责扫描、配置、文件变化监听、模型 API、翻译和剪贴板。
- WebView2 从应用目录的 `web/` 加载本地 HTML/CSS/JS 界面。
- 不依赖 Node.js、Electron 或 Qt。
- 配置与翻译缓存存放在程序目录，源 `SKILL.md` 只读。

## 当前状态

首版功能已实现：多路径扫描、可配置分类、搜索与键盘选择、拖拽归类、复制、文件同步、配置、双协议模型、模型测试和描述翻译。

## 环境要求

- Windows 10/11
- Visual Studio 2022 Build Tools（MSVC x64 + Windows SDK）
- CMake 3.24+
- WebView2 Evergreen Runtime

应用运行时不访问 CDN。首次 CMake 配置需要联网从 NuGet 下载以下固定版本依赖，之后复用构建目录缓存：

- Microsoft.Web.WebView2 `1.0.4258.31`
- ImeSense.Packages.YamlCpp.Runtimes.win-x64 `0.8.0`
- nlohmann.json `3.11.3`

WebView2 Runtime 由系统或安装程序提供；`yaml-cpp.dll` 会在构建后自动复制到 EXE 同目录。

## 构建

在 Visual Studio 2022 Developer PowerShell 中执行：

```powershell
cmake --preset windows-x64
cmake --build --preset debug
```

产物位于 `build/windows-x64/Debug/SkillBox.exe`。

Release 构建：

```powershell
cmake --build .\build\windows-x64 --config Release
```

产物位于 `build/windows-x64/Release/SkillBox.exe`。
构建完成后可双击根目录的 `start-skillbox.bat` 启动。

首次配置会下载 NuGet 依赖。若系统缺少 WebView2 Evergreen Runtime，程序启动时会显示安装提示并退出。

## 验证

在 Developer PowerShell 中执行：

```powershell
cmake --build .\build\windows-x64 --config Release
ctest --test-dir .\build\windows-x64 -C Release --output-on-failure
.\tools\integration-smoke.ps1 `
  -Exe .\build\windows-x64\Release\SkillBox.exe `
  -YamlDll .\build\windows-x64\Release\yaml-cpp.dll
```

最近验证结果：

- Release 构建通过，`SkillBox.exe`、`yaml-cpp.dll` 和本地 `web/` 资源齐全。
- `SkillBoxTests`: 1/1 passed，覆盖配置、DPAPI、路径、扫描、frontmatter、监听、桥、剪贴板、WinHTTP、双协议和批量翻译。
- `integration-smoke.ps1`: 通过，覆盖真实进程启动、程序目录配置、快捷启动工作目录、删除同步、离线路径保留和损坏配置恢复。
- 原生 E2E 通过：真实 Skill 发现后复制得到 `/ui-review`，分类移动写入 `frontend`。
- 界面捕获脚本通过：搜索、折叠/展开、右键菜单、拖拽归类、分类重命名/新增、翻译状态及 420×680、380×560 布局检查。

