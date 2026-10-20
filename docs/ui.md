# Interface

## Catalog

- Five initial categories: 常用、不常用、前端、后端、其他; categories can be renamed or added.
- Category headers collapse independently; “折叠全部” switches the complete tree.
- Search matches command, name, English description, Chinese translation, and category label.
- Leading slashes are hidden in command labels and optional in search; copied commands still include `/`.
- Skill descriptions expand to their full line count while row actions stay visible.
- Click a row to copy `/skill-name`; right-click opens category and retranslation actions.
- Translation markers switch between Chinese and English for one row.

## Settings

Settings replace the catalog view at full height. There is no nested modal.

- The title bar theme switch toggles between the light and dark themes and saves the choice.
- `Skills 路径` manages monitored folders and shows unavailable paths.
- `模型与翻译` starts with no configured provider and asks the user to add one.
- `添加供应商` opens a picker dialog for OpenAI Responses, OpenAI Chat Completions, 智谱 GLM, DeepSeek, 百度千帆（免费额度）, 豆包, 硅基流动, 魔搭社区, or a custom endpoint.
- Added providers appear as independent expandable cards. Each card internally keeps its preset protocol while exposing only Base URL, writable model, and a write-only API key.
- Provider cards and the picker show supplier names only. The model remains inside the expanded card, and the pencil beside a provider name enables inline rename.
- Newly added providers are never selected automatically. The user explicitly chooses `使用`; that ephemeral current provider is used for translation and is not saved across restarts.
- Testing a provider displays the measured latency in its card.
- API keys are never populated back into the input control after saving.

## Keyboard

- `Ctrl+K` or `/`: focus search.
- `ArrowUp` / `ArrowDown`: move through visible results.
- `Enter`: copy the first visible result from search.
- `Escape`: close the context menu, close settings, or clear search.
