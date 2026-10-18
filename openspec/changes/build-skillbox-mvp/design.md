# Design

## Context

参见 `proposal.md`。当前仓库只有可构建的 C++20 空壳，没有既有业务模块。目标环境为 Windows 10/11，WebView2 Runtime、MSVC 和 CMake 已具备；用户提供的参考稿是单文件 HTML，含 CDN、演示数据和浏览器模拟逻辑，只能作为视觉与交互参考。

## Goals / Non-Goals

**Goals:**

- 保持原生 Windows 应用形态，不引入 Node.js、Electron、Qt 或常驻浏览器服务。
- 让 C++ 独占文件、配置、网络、剪贴板和窗口能力，前端只负责渲染和用户输入。
- 保留参考稿的紧凑窄窗、分类折叠、搜索、右键归类和设置面板。
- 对损坏配置、磁盘删除、部分翻译失败和 API 错误提供可恢复状态。

**Non-Goals:**

- 不实现 Skill 编辑、版本管理、同步账号或插件市场。
- 不允许自定义分类，也不实现拖拽排序。
- 不兼容 macOS、Linux 或旧版 Windows。
- 不把翻译结果、分类或界面状态写回源 `SKILL.md`。

## Decisions

### 原生宿主与 WebView2

采用 C++20、Win32、WebView2 和本地 Web 资源。WebView2 通过虚拟主机映射加载 `web/`，避免 `file://` 的限制，并让前端使用本地 ES modules。界面使用 Vanilla HTML/CSS/JS，不引入前端框架。

替代方案：Qt 需要额外运行时且界面重写成本高；纯 Win32 控件难以实现参考稿的排版；Electron 与原生 C++ 目标冲突。

### 线程与通信

Win32 主线程负责窗口、WebView2 和剪贴板；后台任务负责扫描、文件监听和 HTTP。前后端通过带 `id` 的 JSON WebMessage 传递请求和结果，命令名保持稳定，例如 `catalog.refresh`、`settings.update`、`models.list`、`translation.run`。

扫描结果先在后台构建，再一次性替换 UI 快照，避免频繁重绘。HTTP 结果通过主线程回调给 WebView2，长任务支持取消。

### 数据模型与持久化

`config.json` 使用版本化对象，包含 `version`、`paths`、`categoryDefinitions`、`categories`、`translations`、`api` 和 `ui`。`categoryDefinitions` 保存分类稳定标识、显示名称和顺序；`categories` 以规范化 `SKILL.md` 路径映射到分类标识。译文额外保存源描述 SHA-256，源描述变化即失效。

配置先在程序目录写临时文件，再原子替换。启动解析失败时保留 `config.json.corrupt-<timestamp>`，随后创建默认配置。API Key 只存 DPAPI 密文，C++ 不把解密后的密钥返回给 WebView2；前端只显示“已保存”状态或提交新密钥。

替代方案：WebView2 `localStorage` 更省 C++ 代码，但无法可靠满足程序目录配置、DPAPI 和外部文件可见性要求。

### Skills 扫描与解析

使用 `std::filesystem` 递归扫描，限制在配置根目录内并跳过符号链接循环。`SKILL.md` frontmatter 用固定版本的 `yaml-cpp` 解析，JSON 用 `nlohmann/json`。扫描结果按规范化路径去重，并按 `name`、分类和路径稳定排序。

文件监听使用 `ReadDirectoryChangesW` 监视每个可用根目录，事件去抖后触发增量刷新；监听失败或根目录离线时回退到周期扫描。根目录不可用时仅更新状态，不从配置移除。

### 网络与翻译

使用 WinHTTP 发送请求，并包装 OpenAI 兼容和 Anthropic 两套请求适配器。模型列表失败不阻断手动输入。连通性测试发送最小真实请求并记录耗时。

批量翻译采用有限并发，单条失败不中止队列，结果分批持久化。请求体只包含翻译指令、描述和必要生成参数；日志与 UI 错误统一脱敏。

### 窗口与视觉语言

界面以 420×680 的窄窗口为默认尺寸，最小 380×560，可调整大小。自绘标题栏保留置顶、设置和窗口控制；DWM 负责圆角与阴影。设置使用覆盖式全高面板，不堆叠多层 Modal。

WebView2 覆盖完整客户区，因此窗口边缘放置原生缩放热区；前端在边缘或角落触发 `window.resizeStart`，宿主调用 `WM_NCLBUTTONDOWN` 完成原生拖拽缩放。标题栏拖动继续通过 `window.drag`。

视觉沿用参考稿的浅灰层级和蓝色强调色，但移除 emoji 分类、演示按钮、随机状态和半透明卡片堆叠。分类标题为紧凑工具条，Skill 行为高密度列表项，圆角不超过 8px。图标统一使用本地打包的 Lucide SVG，不使用远程资源。Skill 描述允许两行并保持高密度；分类使用低饱和度区分色。

键盘交互补齐为：`Ctrl+K` 或 `/` 聚焦搜索，方向键移动结果，`Enter` 复制，`Esc` 清空或关闭面板。空状态、不可用路径、加载中和失败状态均提供明确文本，不依赖颜色单独表达。

## Risks / Trade-offs

- WebView2 Runtime 缺失会导致窗口无法显示；启动时检测并给出安装说明，构建产物附带引导链接。
- `ReadDirectoryChangesW` 在网络盘或大量事件下可能丢失通知；周期扫描提供最终一致性，因此规格允许最长 2 秒同步延迟。
- 模型服务的模型列表能力并不统一；手动模型输入是必要降级路径。
- 批量翻译可能触发速率限制或产生费用；默认分小批执行，显示进度和失败数量，不自动无限重试。
- DPAPI 绑定当前 Windows 用户，复制配置到另一账号后 API Key 不可用；系统要求用户重新输入。
- YAML 和 WebView2 SDK 增加首次构建下载量；依赖固定版本并缓存到构建目录，避免运行时网络依赖。

## Migration Plan

首次启动创建 `config.json` v1 并扫描默认路径。若旧版或未来版本配置可识别则迁移；不可识别或损坏时备份原文件并创建新默认配置。卸载时保留配置和译文，删除程序目录即可回滚到无应用状态。

## Open Questions

无。模型列表不可用时的手动输入行为已在规格中确定。
