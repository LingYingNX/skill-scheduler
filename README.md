# Skill调度器

<p align="center">
  <img src="web/app-icon.svg" width="96" height="96" alt="Skill调度器图标">
</p>

**Skill调度器**（内部项目名 SkillBox）是一个原生 Windows 的 Skills 浏览、分类、翻译与快速调用工具。它用 C++20 + WebView2 构建，不依赖 Electron、Node.js 或 Qt。

[功能特性](#功能特性) · [安装](#安装) · [使用说明](#使用说明) · [从源码构建](#从源码构建) · [许可证](#许可证)

## 功能特性

- **多路径扫描**：递归扫描多个 Skills 根目录，解析 `SKILL.md` 的 frontmatter。
- **自定义分类**：内置常用、不常用、前端、后端、其他，可重命名、新增、拖拽排序或删除。
- **搜索与键盘操作**：按名称、命令、描述、译文和分类过滤，支持方向键与回车。
- **一键复制**：点击任意 Skill 行即可复制调用命令。
- **中英切换**：每行显示 `英` / `中` 按钮；缺少译文时先调用模型翻译，再自动切换。
- **文件实时同步**：监听 Skills 目录，文件新增、修改、删除后自动刷新。
- **模型供应商管理**：内置 OpenAI Responses、OpenAI Chat Completions、智谱 GLM、DeepSeek、百度千帆、豆包、硅基流动、魔搭社区等预设，也支持自定义接口。
- **三协议支持**：OpenAI Responses、OpenAI Chat Completions、Anthropic Messages，可在供应商卡片中切换。
- **模型下拉与延迟测试**：拉取供应商模型列表，测试连接延迟，自动把可用供应商设为当前。
- **安全存储密钥**：API Key 使用 Windows DPAPI 加密后写入配置文件，仅当前用户可解密。
- **亮色 / 深色主题**：跟随系统配色偏好，也可手动切换。

## 系统要求

- Windows 10 / 11 x64
- [Microsoft Edge WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/)（Windows 11 通常已内置）

## 安装

1. 在 [Releases](https://github.com/LingYingNX/skill-scheduler/releases) 下载 `SkillScheduler-1.0-Setup.exe`。
2. 运行安装程序，按向导完成安装。默认安装到 `%LOCALAPPDATA%\Programs\SkillScheduler`，无需管理员权限。
3. 从开始菜单或桌面快捷方式启动「Skill调度器」。

首次启动会在安装目录生成 `config.json`。通过 Windows「设置 → 应用」或安装目录中的卸载程序即可卸载；`config.json` 会保留，方便重装后继续使用。

## 使用说明

### 添加 Skills 路径

打开右上角设置 → `Skills 路径` → 输入或浏览包含 `SKILL.md` 的文件夹，点击添加。路径不可用时会在顶部提示，但配置会保留。

### 分类管理

- 拖拽 Skill 到分类标题即可归类。
- 拖拽分类标题可上下调整顺序。
- 分类右侧的圆形叉号可删除该分类；确认后其中的 Skills 会自动移回「其他」。
- 双击分类名可重命名，底部「新分类」可新增分类。

### 搜索与复制

点击搜索框或按 `Ctrl+K` / `/` 聚焦搜索；`↑` `↓` 选择可见结果，`Enter` 复制第一个结果。点击任意 Skill 行也会复制。

### 中英切换

每行 Skill 名称右侧有 `英` 和 `中` 两个按钮：

- 有缓存时立即切换。
- 没有目标语言译文时，调用当前供应商翻译，成功后自动切换并缓存。

### 模型与翻译

设置 → `模型与翻译` → `添加供应商`，选择预设或自定义：

1. 填写 API Key（自动加密保存）。
2. 点击刷新按钮拉取模型列表，或选择「自定义…」手动输入模型 ID。
3. 点击「测试模型」验证连通性，成功后该供应商自动成为当前供应商。
4. 回到列表点击 `翻译` 批量翻译，或用每行的 `中` 按钮按需翻译。

### 快捷键

| 快捷键 | 功能 |
| --- | --- |
| `Ctrl+K` / `/` | 聚焦搜索 |
| `↑` / `↓` | 选择可见结果 |
| `Enter` | 复制当前结果 |
| `Esc` | 关闭菜单 / 设置 / 清空搜索 |

## 数据与隐私

- 配置与翻译缓存：程序目录下的 `config.json`。
- API Key：使用 Windows DPAPI 加密，密文只对当前 Windows 用户可解密。
- 程序不访问 CDN；只有当你主动测试模型或翻译时，才会向所配置的 API 端点发送描述文本。
- 翻译请求只包含需要翻译的描述，不发送路径、完整配置或 API Key 到请求体。

## 从源码构建

需要：

- Visual Studio 2022 Build Tools（MSVC x64 + Windows SDK）
- CMake 3.24+
- 首次配置时可联网下载 NuGet 依赖

```powershell
cmake --preset windows-x64
cmake --build .\build\windows-x64 --config Release
ctest --test-dir .\build\windows-x64 -C Release --output-on-failure
```

产物位于 `build/windows-x64/Release/`，包含 `SkillBox.exe`、`yaml-cpp.dll` 和 `web/` 资源目录。

依赖版本固定为 Microsoft.Web.WebView2 `1.0.4258.31`、ImeSense.Packages.YamlCpp.Runtimes.win-x64 `0.8.0`、nlohmann.json `3.11.3`，并在 CMake 中校验 SHA-256。

### 构建安装包

安装 [Inno Setup 6](https://jrsoftware.org/isinfo.php)，然后在仓库根目录执行：

```powershell
& "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe" installer.iss
```

生成的安装程序位于 `out/SkillScheduler-1.0-Setup.exe`。

## 项目结构

```text
assets/         应用图标与 Windows 资源
src/app/        应用宿主、窗口与命令分发
src/core/       配置、扫描、路径、文件监听
src/net/        模型客户端、HTTP、翻译
src/platform/   剪贴板、DPAPI、文件夹选择
tests/          单元测试
web/            WebView2 界面资源
installer.iss   Inno Setup 安装包脚本
```

## 常见问题

**启动提示缺少 WebView2 Runtime**

安装 Microsoft Edge WebView2 Evergreen Runtime 后重试。

**路径显示不可用**

移动硬盘或网络路径可能暂时离线；路径与缓存会保留，恢复后自动重新扫描。

**翻译提示未选择供应商**

至少添加一个供应商并保存 API Key。测试模型成功或保存第一个密钥后会自动设为当前供应商。

## 许可证

本项目基于 [GNU General Public License v3.0](LICENSE) 发布。

## 版本

当前版本：**1.0**