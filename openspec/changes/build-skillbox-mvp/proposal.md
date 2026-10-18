# Proposal

## Why

本机 Skills 数量多、目录深，Codex 内逐层查找命令效率低。需要一个原生 Windows 工具，集中浏览、分类、搜索并一键复制 Skills 命令，同时在不修改源文件的前提下提供描述翻译。

## What Changes

- 新增 C++20 原生桌面程序，使用 WebView2 承载本地 HTML/CSS/JS 界面。
- 递归扫描多个 Skills 根目录，解析每个 `SKILL.md` 的 `name` 与 `description`。
- 提供可配置分类树、分类折叠、全文搜索、右键或拖拽归类、点击复制 `/skill-name`。
- 支持分类重命名和新增分类，内置分类仅作为初始配置。
- 监听磁盘变化；Skill 文件删除后同步移除列表项和翻译缓存，整个根目录离线时保留路径配置并显示不可用。
- 在程序目录自动维护 `config.json`，保存路径、分类、模型设置、翻译缓存和界面状态。
- 支持 OpenAI 兼容协议与 Anthropic 原生协议，拉取模型并执行真实连通性测试。
- 支持单条和批量翻译 Skill 描述，译文只写入配置文件，不修改 `SKILL.md`。
- 界面沿用参考 HTML 的 Windows 11 窄窗口与分类树结构，移除演示假数据、CDN 依赖和装饰性噪音。
- 支持原生窗口边缘/角落拖拽缩放；窄窗口中的描述最多显示两行。
- 首版不提供分类删除、拖拽排序、Skill 内容编辑、账号同步和跨平台支持。

## Capabilities

### New Capabilities

- `skills-catalog`: Skills 多路径发现、元数据解析、分类、检索、复制和磁盘删除同步。
- `model-translation`: OpenAI/Anthropic 模型配置、模型发现、连通性测试和描述翻译缓存。
- `app-configuration`: 程序目录配置持久化、路径管理、敏感信息保护与异常路径状态。

### Modified Capabilities

无。

## Impact

- 影响 C++ 应用入口、WebView2 宿主、本地 Web 资源和 CMake 构建配置。
- 引入 WebView2 SDK、JSON/YAML 解析能力和 Windows 网络、文件、剪贴板及 DPAPI API。
- 新增程序目录下的 `config.json`；源 Skills 文件和描述语言保持不变。
- 需要覆盖扫描、配置损坏恢复、API 错误、翻译失效和磁盘删除等自动化测试。
