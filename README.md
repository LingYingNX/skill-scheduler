# Skill调度器

<p align="center">
  <img src="web/app-icon.svg" width="88" height="88" alt="Skill调度器图标">
</p>

Windows 原生的 Skills 浏览、分类、翻译与快速调用工具（C++20 + WebView2）。单文件运行，无需安装。
<img width="420" height="680" alt="image" src="https://github.com/user-attachments/assets/a7ed70a5-edc5-41fa-9842-b99320179356" />
<img width="420" height="680" alt="image" src="https://github.com/user-attachments/assets/93823ad3-fba8-4323-b32d-32c2e3a2801c" />

## 功能

- 多路径扫描 `SKILL.md`，支持搜索、分类与键盘操作
- 点击 Skill 复制命令；`英` / `中` 一键切换，缺少译文时自动翻译
- 多供应商管理，支持 OpenAI Responses、OpenAI Chat Completions、Anthropic Messages
- 模型下拉选择、延迟测试，API Key 使用 Windows DPAPI 加密保存
- 分类拖拽排序，删除分类后其中的 Skills 自动移回「其他」
- 亮色 / 深色主题，文件变更实时同步

## 从源码构建

需要 Visual Studio 2022 Build Tools 与 CMake 3.24+：

```powershell
cmake --preset windows-x64
cmake --build .\build\windows-x64 --config Release
```

产物为单个 `build/windows-x64/Release/SkillBox.exe`。

## 许可证

[GNU General Public License v3.0](LICENSE)
