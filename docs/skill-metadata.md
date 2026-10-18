# Skill Metadata

SkillBox reads only the YAML frontmatter at the top of each `SKILL.md`.

## Supported Frontmatter

```yaml
---
name: example-skill
description: Single-line quoted or plain description
---
```

```yaml
---
name: example-skill
description: >
  A folded description that
  becomes one paragraph.
---
```

```yaml
---
name: example-skill
description: |
  A literal description that
  preserves line breaks.
---
```

## Behavior

- `name` becomes the copied command `/name`.
- Missing `name` falls back to the parent directory name and adds a warning.
- Missing `description` loads the Skill with an empty description and a warning.
- Invalid YAML or missing delimiters skips that file without stopping other Skills.
- Source files are opened read-only and are never rewritten.
