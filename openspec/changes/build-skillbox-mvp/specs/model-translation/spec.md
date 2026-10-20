# Spec Delta

## Purpose

让用户配置 OpenAI Chat Completions、OpenAI Responses 或 Anthropic Messages 模型服务，验证模型可用性，并把 Skills 英文描述翻译为仅存储在本地配置中的中文缓存。

## ADDED Requirements

### Requirement: 三协议模型服务
系统 SHALL 支持 OpenAI Chat Completions、OpenAI Responses 和 Anthropic Messages 协议，并保存当前协议、Base URL、模型名和加密 API Key。

#### Scenario: 切换协议
- **WHEN** 用户从一种协议切换到另一种协议
- **THEN** 系统加载该协议对应的默认 Base URL 和模型候选，不沿用另一种协议的地址

#### Scenario: 自定义服务地址
- **WHEN** 用户填写有效的自定义 Base URL
- **THEN** 系统使用该地址构造协议对应的请求

### Requirement: 供应商预设与当前模型
系统 SHALL 初始不启用任何供应商，并通过添加供应商弹窗提供 OpenAI Responses、OpenAI Chat Completions、智谱 GLM、DeepSeek、百度千帆、豆包、硅基流动、魔搭社区和自定义地址预设。供应商卡片和添加弹窗 SHALL 只显示供应商名称，不显示底层协议名称或模型副标题。

#### Scenario: 添加供应商
- **WHEN** 用户点击“添加供应商”并在弹窗中选择一个预设
- **THEN** 系统把该供应商加入设置列表并显示为独立可折叠卡片，不自动选择当前供应商

#### Scenario: 重命名供应商
- **WHEN** 用户点击供应商名称旁的画笔并提交新名称
- **THEN** 系统保存该供应商的独立显示名称，不影响其协议、地址、模型或密钥

#### Scenario: 移除供应商
- **WHEN** 用户移除一个供应商
- **THEN** 系统删除该供应商卡片及其本地密钥；若移除的是当前供应商，则清空当前选择

#### Scenario: 保存供应商密钥
- **WHEN** 用户展开供应商卡片并粘贴 API Key
- **THEN** 系统只加密保存该供应商的密钥，不影响其他供应商配置

#### Scenario: 选择当前供应商
- **WHEN** 用户点击某个供应商的“使用”
- **THEN** 当前运行中的单条和批量翻译使用该供应商当前选择的模型与地址，该选择不写入配置

#### Scenario: 测试供应商延迟
- **WHEN** 用户测试某个供应商的模型
- **THEN** 系统在该供应商卡片内显示请求是否成功以及以毫秒计的延迟

### Requirement: 模型列表拉取
系统 SHALL 允许用户从已配置服务拉取可用模型，并在拉取失败时保留手动填写模型的入口。

#### Scenario: 拉取成功
- **WHEN** 模型列表请求成功并返回一个或多个模型
- **THEN** 系统显示模型候选项并保留当前有效选择

#### Scenario: 拉取失败
- **WHEN** 模型列表请求因鉴权、网络或服务能力失败
- **THEN** 系统显示简短错误原因且允许用户手动输入模型名

### Requirement: 模型连通性测试
系统 SHALL 使用当前协议、地址、密钥和模型发送一次最小真实请求，并显示成功或失败结果。

#### Scenario: 测试成功
- **WHEN** 模型返回有效响应
- **THEN** 系统显示成功状态和以毫秒计的延迟

#### Scenario: 测试失败
- **WHEN** 请求超时、鉴权失败或服务返回错误
- **THEN** 系统显示失败状态和可操作错误摘要，不把 API Key 写入日志或界面

### Requirement: 描述翻译
系统 SHALL 支持翻译单个 Skill 描述或批量翻译尚未缓存的英文描述，并把结果写入本地译文缓存。

#### Scenario: 单条翻译
- **WHEN** 用户对某个未翻译 Skill 请求中文翻译
- **THEN** 系统调用当前可用模型并把译文关联到该 Skill

#### Scenario: 批量翻译部分失败
- **WHEN** 批量翻译中个别请求失败
- **THEN** 系统保留成功结果、继续处理其他条目，并汇总失败数量

#### Scenario: 已存在译文
- **WHEN** 用户再次翻译已有有效缓存的 Skill
- **THEN** 系统直接显示缓存译文，除非用户明确重新翻译

#### Scenario: 模型返回推理过程
- **WHEN** 模型把分析、指令复述或原文混入翻译结果
- **THEN** 系统只保留最终中文段落，去除标签、引号和推理内容

#### Scenario: 译文不可用
- **WHEN** 模型响应无法恢复出有效中文译文
- **THEN** 系统把该条标记为翻译失败，不显示也不缓存混杂内容

### Requirement: 译文缓存失效
系统 SHALL 以 Skill 规范化文件路径和原描述内容指纹识别译文缓存，源描述变化后不得继续使用旧译文。

#### Scenario: 描述发生变化
- **WHEN** Skill 的英文描述内容变化
- **THEN** 系统把对应旧译文标记为无效并在界面显示未翻译状态

#### Scenario: Skill 被删除
- **WHEN** Skill 文件从磁盘删除
- **THEN** 系统从配置中移除对应译文缓存

### Requirement: 翻译数据边界
系统 SHALL 只向用户配置的模型服务发送生成译文所需的描述文本和指令，不得上传 Skill 文件内容、路径列表或 API Key 之外的本地配置。

#### Scenario: 翻译请求内容
- **WHEN** 系统发起翻译请求
- **THEN** 请求体只包含模型指令、待翻译描述和生成参数
