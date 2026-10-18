# Model Providers

## OpenAI Compatible

- List models: `GET {baseUrl}/models`
- Complete: `POST {baseUrl}/chat/completions`
- Authentication: `Authorization: Bearer <key>`

## Anthropic

- List models: `GET {baseUrl}/models`
- Complete: `POST {baseUrl}/messages`
- Authentication: `x-api-key: <key>`
- Required version header: `anthropic-version: 2023-06-01`

If model listing is unavailable, the editable model field remains usable.

## Translation

Translation requests send only the translation instructions, the source description, and generation parameters. They never send the Skill path, path list, configuration, or API key in the request body.

Translations are valid only while the stored source-description SHA-256 matches the current description. A changed or deleted Skill invalidates and removes its cached translation.

## Errors

Authentication failures, network timeouts, non-2xx responses, and malformed provider responses surface as concise UI errors. API keys are not included in error text or logs.
