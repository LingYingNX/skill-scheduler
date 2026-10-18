# Spec Delta

## Purpose

让用户以统一分类树浏览本机多个目录中的 Skills，快速搜索、折叠并复制 Codex 命令，同时持续反映磁盘上的真实文件状态。

## ADDED Requirements

### Requirement: 多路径递归发现
系统 SHALL 接受一个或多个本地 Skills 根目录，并递归发现其中的 `SKILL.md` 文件。

#### Scenario: 发现深层 Skills
- **WHEN** 配置的根目录在任意深度包含 `SKILL.md`
- **THEN** 系统将该文件对应的目录识别为一个 Skill

#### Scenario: 重叠根目录去重
- **WHEN** 两个已配置根目录指向同一 `SKILL.md` 或相同真实路径
- **THEN** 系统只显示一个 Skill 条目

#### Scenario: 根目录暂时不可用
- **WHEN** 某个已配置根目录不存在或无法读取
- **THEN** 系统保留该路径配置并标记路径不可用，不把该路径自动删除

### Requirement: Skill 元数据解析
系统 SHALL 从 `SKILL.md` 的 YAML frontmatter 读取 `name` 和 `description`，并保留 UTF-8 文本。

#### Scenario: 多行描述
- **WHEN** `description` 使用 `>`、`|`、引号或其他合法 YAML 标量形式
- **THEN** 系统解析出完整描述文本，不显示 YAML 标记碎片

#### Scenario: 缺少名称
- **WHEN** frontmatter 缺少有效 `name`
- **THEN** 系统使用 `SKILL.md` 所在目录名作为命令名并标记该项存在元数据警告

#### Scenario: 损坏的 frontmatter
- **WHEN** 单个 `SKILL.md` 的 frontmatter 无法解析
- **THEN** 系统跳过该条目并继续加载其他 Skills，不中断整个目录扫描

### Requirement: 可配置分类树
系统 SHALL 以“常用、不常用、前端、后端、其他”为初始分类，并允许用户重命名现有分类和新增分类；分类配置 SHALL 持久化到 `config.json`。

#### Scenario: 新 Skill 默认分类
- **WHEN** 系统第一次发现某个 Skill 且配置中没有该 Skill 的分类记录
- **THEN** 该 Skill 显示在“其他”分类

#### Scenario: 持久化分类变更
- **WHEN** 用户通过右键菜单或拖拽把 Skill 移入另一分类
- **THEN** 系统立即更新分类树并把归属写入 `config.json`

#### Scenario: 分类折叠
- **WHEN** 用户点击分类标题或“折叠全部”
- **THEN** 对应条目收起并更新三角图标，再次操作可恢复展开

#### Scenario: 重命名分类
- **WHEN** 用户双击分类标题并提交新的非空名称
- **THEN** 系统更新分类显示名称并保持 Skill 归属不变

#### Scenario: 新增分类
- **WHEN** 用户在分类树底部输入新的非空分类名称
- **THEN** 系统在分类树底部创建分类，并在 `config.json` 中保存其稳定标识和名称

#### Scenario: 展开全部
- **WHEN** 分类树处于全部折叠状态且用户点击“展开全部”
- **THEN** 所有分类立即展开，且此前单独折叠的分类也不再保持折叠

### Requirement: 分类拖放
系统 SHALL 允许用户按住 Skill 行并将其拖到任意分类标题或分类区域。

#### Scenario: 拖拽归类
- **WHEN** 用户把某个 Skill 拖放到目标分类并释放
- **THEN** 系统更新该 Skill 的分类、移动界面行并保存配置

#### Scenario: 拖放到原分类
- **WHEN** 用户把 Skill 拖回当前分类
- **THEN** 系统保持原有分类且不产生重复记录

### Requirement: 搜索与命令复制
系统 SHALL 支持按命令名、英文描述、中文译文和分类名称搜索，并允许用户点击结果复制 `/skill-name`。

#### Scenario: 搜索命中译文
- **WHEN** 搜索文本只出现在 Skill 的中文译文或分类名称中
- **THEN** 该 Skill 仍显示在搜索结果中

#### Scenario: 点击复制
- **WHEN** 用户点击某个 Skill 条目
- **THEN** 系统把 `/` 加 frontmatter `name` 复制到系统剪贴板并显示成功提示

#### Scenario: 无匹配结果
- **WHEN** 当前搜索文本没有命中任何 Skill
- **THEN** 系统显示明确空状态且保留清空搜索的入口

#### Scenario: 窄窗口描述
- **WHEN** 窗口缩窄且 Skill 描述超过一行
- **THEN** 描述最多显示两行并保持操作按钮可见，不截断为单行

### Requirement: 磁盘变化同步
系统 SHALL 在 Skill 文件被新增、修改或删除后自动刷新目录和元数据。

#### Scenario: 删除 Skill 文件
- **WHEN** 已加载的 `SKILL.md` 在磁盘被删除
- **THEN** 系统在 2 秒内从列表移除该 Skill 并删除其翻译缓存

#### Scenario: 修改 Skill 描述
- **WHEN** 已加载的 `SKILL.md` frontmatter 发生变化
- **THEN** 系统刷新命令名与描述，并使旧描述对应的译文失效

#### Scenario: 监听失败
- **WHEN** 文件变化监听无法启动或意外停止
- **THEN** 系统继续使用周期扫描同步状态，并在界面显示监控降级提示
