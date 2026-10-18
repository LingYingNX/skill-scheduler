# WebView2 Bridge

The WebView posts one JSON object per command:

```json
{
  "id": "request-1",
  "command": "clipboard.copy",
  "payload": {
    "text": "/example-skill"
  }
}
```

Successful responses:

```json
{
  "id": "request-1",
  "ok": true,
  "data": {
    "text": "/example-skill"
  }
}
```

Failed responses:

```json
{
  "id": "request-1",
  "ok": false,
  "error": {
    "code": "clipboard_error",
    "message": "unable to open the clipboard"
  }
}
```

The bridge preserves the request `id`, rejects malformed JSON, and never includes the API key in response data.
