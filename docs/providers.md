# Model Providers

## OpenAI Chat Completions

- List models: `GET {baseUrl}/models`
- Complete: `POST {baseUrl}/chat/completions`
- Authentication: `Authorization: Bearer <key>`

## OpenAI Responses

- List models: `GET {baseUrl}/models`
- Complete: `POST {baseUrl}/responses`
- Authentication: `Authorization: Bearer <key>`
- Response text is read from the typed `output` array.

## Anthropic

- List models: `GET {baseUrl}/models`
- Complete: `POST {baseUrl}/messages`
- Authentication: `x-api-key: <key>`
- Required version header: `anthropic-version: 2023-06-01`

The model field is a dropdown filled by model detection. If a needed model is not returned by the provider, choose `自定义…` and type the model ID manually.

## Presets

| Provider | Protocol | Base URL | Default model |
| --- | --- | --- | --- |
| OpenAI Responses | OpenAI Responses | `https://api.openai.com/v1` | `gpt-4o-mini` |
| OpenAI Chat Completions | OpenAI Chat Completions | `https://api.openai.com/v1` | `gpt-4o-mini` |
| Anthropic Messages | Anthropic Messages | `https://api.anthropic.com/v1` | `claude-sonnet-4-20250514` |
| 智谱 GLM | OpenAI Chat Completions | `https://open.bigmodel.cn/api/paas/v4` | `glm-4-flash` |
| DeepSeek | OpenAI Chat Completions | `https://api.deepseek.com/v1` | `deepseek-chat` |
| 百度千帆（免费额度） | OpenAI Chat Completions | `https://qianfan.baidubce.com/v2` | `ernie-speed-128k` |
| 豆包 | OpenAI Chat Completions | `https://ark.cn-beijing.volces.com/api/v3` | `doubao-1-5-lite-32k-250115` |
| 硅基流动 | OpenAI Chat Completions | `https://api.siliconflow.cn/v1` | `Qwen/Qwen2.5-7B-Instruct` |
| 魔搭社区 | OpenAI Chat Completions | `https://api-inference.modelscope.cn/v1` | `Qwen/Qwen2.5-7B-Instruct` |

Preset values remain editable because provider model names and account capabilities can change.

## Translation

Translation requests send only the translation instructions, the source description, and generation parameters. They never send the Skill path, path list, configuration, or API key in the request body.

Translations are valid only while the stored source-description SHA-256 matches the current description. A changed or deleted Skill invalidates and removes its cached translation.

Translation output is normalized before caching: code fences, labels, quotes, and reasoning are stripped, and only the final Chinese passage is kept. A response without a usable Chinese result is treated as a failed translation and is not shown or cached.

## Errors

Authentication failures, network timeouts, non-2xx responses, and malformed provider responses surface as concise UI errors. API keys are not included in error text or logs.
