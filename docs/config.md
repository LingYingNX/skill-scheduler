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
  "api": {
    "protocol": "openai",
    "baseUrl": "https://api.openai.com/v1",
    "model": "gpt-4o-mini",
    "apiKeyCipher": "DPAPI-BASE64-CIPHERTEXT"
  },
  "ui": {
    "pinned": false,
    "allCollapsed": false,
    "collapsedCategories": []
  }
}
```

## Security Boundary

- Source `SKILL.md` files are never modified.
- API keys are encrypted with Windows DPAPI and stored only as `apiKeyCipher`.
- Decrypted API keys stay inside the native process and are not sent to WebView2 with configuration state.
- Invalid config files are copied to `config.json.corrupt-<timestamp>` before defaults are recreated.
