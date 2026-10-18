# Interface

## Catalog

- Five fixed categories: 常用、不常用、前端、后端、其他.
- Category headers collapse independently; “折叠全部” switches the complete tree.
- Search matches command, name, English description, Chinese translation, and category label.
- Click a row to copy `/skill-name`; right-click opens category and retranslation actions.
- Translation markers switch between Chinese and English for one row.

## Settings

Settings replace the catalog view at full height. There is no nested modal.

- `Skills 路径` manages monitored folders and shows unavailable paths.
- `模型与翻译` stores protocol, Base URL, model, and a write-only API Key state.
- API keys are never populated back into the input control after saving.

## Keyboard

- `Ctrl+K` or `/`: focus search.
- `ArrowUp` / `ArrowDown`: move through visible results.
- `Enter`: copy the first visible result from search.
- `Escape`: close the context menu, close settings, or clear search.
