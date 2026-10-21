<!-- FOR AI AGENTS - Human readability is a side effect, not a goal -->
<!-- Managed by agent: keep sections and order; edit content, not structure -->
<!-- Last updated: 2026-10-21 | Last verified: 2026-10-21 -->

# AGENTS.md

**Precedence:** the **closest `AGENTS.md`** to the files you're changing wins. Root holds global defaults only.

## 项目

- Windows 原生 Skills 浏览、分类、翻译与快速调用工具
- 技术栈：C++20 + Win32 + WebView2 + 原生 HTML/CSS/JS
- 发布形态：单个 `SkillBox.exe`，无安装器、无运行时 DLL
- 许可证：GPL-3.0

## 目录结构

| 路径 | 责任 |
| --- | --- |
| `src/app/` | 应用宿主、Win32 窗口、WebView2 初始化、命令分发 |
| `src/core/` | 配置、`SKILL.md` 扫描与解析、分类、文件监听 |
| `src/net/` | WinHTTP、模型客户端、翻译与输出清洗 |
| `src/platform/` | 剪贴板、DPAPI、文件夹选择 |
| `web/` | WebView2 界面源码，构建时内嵌进 exe |
| `assets/` | 图标、`app.rc`、内嵌资源脚本 `web.rc` |
| `tests/` | CTest 单元测试 |
| `CMakeLists.txt` | 依赖、目标、资源嵌入、编译选项 |
| `CMakePresets.json` | `windows-x64` 配置与 Debug/Release 构建预设 |

## 构建与测试

在 Visual Studio 2022 Developer PowerShell（x64）中执行：

| 目的 | 命令 |
| --- | --- |
| 配置 | `cmake --preset windows-x64` |
| Release 构建 | `cmake --build --preset release` |
| Debug 构建 | `cmake --build --preset debug` |
| 单元测试 | `ctest --test-dir .\build\windows-x64 -C Release --output-on-failure` |
| 运行产物 | `build\windows-x64\Release\SkillBox.exe` |

依赖由 CMake FetchContent 固定版本并校验 SHA-256：yaml-cpp `0.8.0`（静态库）、nlohmann.json `3.11.3`、WebView2 SDK `1.0.4258.31`。

## 关键约束

- **单文件发布**：不要引入新的运行时 DLL；yaml-cpp 必须保持静态链接。
- `web/` 不是运行时目录：它由 `assets/web.rc` 编译进 exe，启动时释放到 `%LOCALAPPDATA%\SkillBox\runtime\1.0`。修改 `web/` 后必须重新构建，改动才会进入 exe。
- `assets/web.rc` 依赖 `SkillBox` 的 `PRE_BUILD` 触达强制重编；不要移除该逻辑。
- 配置始终写入 exe 同级 `config.json`；API Key 只以 DPAPI 密文保存，禁止写入明文、日志或错误信息。
- 版本号需四处一致：`CMakeLists.txt` 的 `project(VERSION)`、`assets/app.rc`、`src/app/Application.cpp` 的 `app.ping`、`web/index.html` 左上角。
- `assets/app.rc` 使用 UTF-8 BOM 与 `#pragma code_page(65001)`；其余源码保持 UTF-8。
- 界面与文案以中文为主。

## 改哪里

| 需求 | 入口 |
| --- | --- |
| 新增/修改命令 | `src/app/Application.cpp` |
| 窗口、托盘、WebView2、资源释放 | `src/app/MainWindow.cpp` |
| 配置字段与迁移 | `src/core/Config.h` / `Config.cpp` |
| 技能扫描、分类增删排序 | `src/core/SkillCatalog.cpp` |
| frontmatter 解析 | `src/core/SkillParser.cpp` |
| 模型协议与请求 | `src/net/ModelClient.cpp` |
| 翻译提示词与校验 | `src/net/TranslationService.cpp` / `TranslationOutput.cpp` |
| 全部界面逻辑 | `web/app.js` |
| 全部界面样式 | `web/styles.css` |
| 界面 HTML 与图标符号 | `web/index.html` |

## 验证清单

1. Release 构建通过。
2. `ctest` 通过（当前 1/1）。
3. 只拷贝 `SkillBox.exe` 到空目录仍能启动。
4. 首次启动在同级目录生成 `config.json`。
5. 修改 `web/` 后，运行目录中的 `app.js` / `styles.css` 与源码一致。

## 不要做

- 不要提交 `build/`、`out/` 或任何 `config.json`（可能含密钥）。
- 不要重新引入安装器、便携压缩包或运行时资源目录。
- 不要更换 GPL-3.0 许可证。
## Index of scoped AGENTS.md

本项目为单根目录，无 scoped `AGENTS.md`。若未来新增子模块，优先在对应目录放置 scoped 文件并在本节登记。