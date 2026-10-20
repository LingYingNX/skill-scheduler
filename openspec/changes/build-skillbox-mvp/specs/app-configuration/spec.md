# Spec Delta

## Purpose

安全、可恢复地保存 Skills 路径、分类、模型设置、翻译缓存和界面状态，使程序重启后保持用户配置，同时不修改任何源 Skill 文件。

## ADDED Requirements

### Requirement: 程序目录配置
系统 SHALL 在可执行文件所在目录维护 `config.json`，并在文件不存在时自动创建带版本号的默认配置。配置 SHALL 保存分类稳定标识、显示名称和顺序，以及每个已添加模型供应商的协议、地址、模型、密钥密文和启用列表；当前供应商仅存在于运行时，不写入配置。

#### Scenario: 首次启动
- **WHEN** 可执行文件目录没有 `config.json`
- **THEN** 系统创建默认配置并继续启动

#### Scenario: 从快捷方式启动
- **WHEN** 用户通过工作目录不同的快捷方式启动程序
- **THEN** 系统仍读取可执行文件所在目录的 `config.json`

### Requirement: 配置自动保存
系统 SHALL 在路径、分类、API 设置、翻译缓存或界面状态发生变化后自动保存配置，不要求用户点击“保存”。

#### Scenario: 更新分类
- **WHEN** 用户更改 Skill 分类
- **THEN** 系统在界面更新完成后把新分类写入 `config.json`

#### Scenario: 更新分类名称
- **WHEN** 用户重命名分类或新增分类
- **THEN** 系统保存分类标识、名称和顺序，重启后保持相同分类树

#### Scenario: 切换主题
- **WHEN** 用户切换亮色或深色主题
- **THEN** 系统立即更新界面并把主题选择保存到 `config.json`

#### Scenario: 原子写入
- **WHEN** 系统正在保存配置
- **THEN** 系统先写临时文件再替换目标文件，避免进程中断留下半个 JSON 文件

### Requirement: 配置损坏恢复
系统 SHALL 在 `config.json` 无法解析或结构无效时保留损坏文件并恢复为可用默认配置。

#### Scenario: JSON 损坏
- **WHEN** 启动时发现 `config.json` 不是有效 JSON
- **THEN** 系统把原文件备份为带时间戳的文件，创建新的默认配置并提示用户

### Requirement: Skills 路径管理
系统 SHALL 允许用户通过文件夹选择框或文本输入添加多个 Skills 路径，并允许移除已保存路径。

#### Scenario: 添加重复路径
- **WHEN** 用户添加与现有路径规范化后相同的路径
- **THEN** 系统不创建重复项并提示路径已存在

#### Scenario: 添加无效路径
- **WHEN** 用户输入不存在或不是目录的路径
- **THEN** 系统拒绝添加并显示可理解的错误提示

#### Scenario: 移除路径
- **WHEN** 用户移除一个监控路径
- **THEN** 系统从配置和当前列表移除该路径下的 Skills，但绝不删除磁盘文件

### Requirement: 敏感信息保护
系统 SHALL 使用 Windows DPAPI 加密 API Key，配置文件不得以明文保存密钥。

#### Scenario: 保存 API Key
- **WHEN** 用户输入或更新 API Key
- **THEN** 系统只把 DPAPI 加密后的密文写入 `config.json`

#### Scenario: 密文无法解密
- **WHEN** 当前 Windows 用户无法解密已保存的 API Key
- **THEN** 系统清空内存中的密钥、保留其他配置并要求用户重新输入密钥

### Requirement: 源文件只读约束
系统 SHALL 将 Skills 源目录视为只读，不得因分类、翻译或界面状态修改 `SKILL.md`。

#### Scenario: 保存翻译
- **WHEN** 系统获得新的中文译文
- **THEN** 译文只写入 `config.json`，源 `SKILL.md` 内容与编码保持不变
