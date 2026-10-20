# Configuration

`config.json` is created beside `SkillBox.exe` on first launch.

## Schema

```json
{
  "version": 1,
  "paths": ["C:\\Users\\Example\\.agents\\skills"],
  "categories": {
    "C:\\Users\\Example\\.agents\\skills\\demo\\SKILL.md": "favorite"
  },
  "translations": {
    "C:\\Users\\Example\\.agents\\skills\\demo\\SKILL.md": {
      "sourceHash": "sha256-of-description",
      "text": "中文译文",
      "updatedAt": "2026-10-18T09:00:00Z"
    }
  },
  "enabledProviderIds": ["deepseek"],
  "providers": {
    "deepseek": {
      "name": "DeepSeek 主账号",
      "protocol": "openai",
      "baseUrl": "https://api.deepseek.com/v1",
      "model": "deepseek-chat",
      "apiKeyCipher": "DPAPI-BASE64-CIPHERTEXT"
    }
  },
  "ui": {
    "pinned": false,
    "allCollapsed": false,
    "collapsedCategories": [],
    "theme": "dark"
  }
}
```

## Security Boundary

- Source `SKILL.md` files are never modified.
- Every provider keeps its own API key encrypted with Windows DPAPI as `apiKeyCipher`.
- Decrypted API keys stay inside the native process and are not sent to WebView2 with configuration state.
- Translation requests use the provider selected as the default.
- Invalid config files are copied to `config.json.corrupt-<timestamp>` before defaults are recreated.
