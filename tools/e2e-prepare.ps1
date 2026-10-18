param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$YamlDll,
  [switch]$SeedTranslation
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$workRoot = Join-Path $projectRoot 'build\e2e'
$appDir = Join-Path $workRoot 'app'
$skillsDir = Join-Path $workRoot 'skills'

if (Test-Path -LiteralPath $workRoot) {
  Remove-Item -LiteralPath $workRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $appDir | Out-Null
New-Item -ItemType Directory -Path $skillsDir | Out-Null
Copy-Item -LiteralPath $Exe -Destination (Join-Path $appDir 'SkillBox.exe')
Copy-Item -LiteralPath $YamlDll -Destination (Join-Path $appDir (Split-Path -Leaf $YamlDll))
Copy-Item -LiteralPath (Join-Path $projectRoot 'web') -Destination $appDir -Recurse

$items = @(
  @{ Directory = 'frontend\ui-review'; Name = 'ui-review'; Description = 'Review interface hierarchy, spacing, typography, and interaction polish.' },
  @{ Directory = 'frontend\motion-web'; Name = 'motion-web'; Description = 'Design and build motion-first websites with scroll-linked scenes.' },
  @{ Directory = 'backend\api-security'; Name = 'api-security'; Description = 'Audit authentication, authorization, rate limits, and sensitive data paths.' },
  @{ Directory = 'workflow\openspec-apply-change'; Name = 'openspec-apply-change'; Description = 'Implement tracked tasks from an OpenSpec change.' }
)
foreach ($item in $items) {
  $directory = Join-Path $skillsDir $item.Directory
  New-Item -ItemType Directory -Path $directory | Out-Null
  @"
---
name: $($item.Name)
description: $($item.Description)
---
"@ | Set-Content -LiteralPath (Join-Path $directory 'SKILL.md') -Encoding utf8
}

$config = [ordered]@{
  version = 1
  paths = @($skillsDir)
  categories = [ordered]@{
    (Join-Path $skillsDir 'frontend\ui-review\SKILL.md') = 'favorite'
    (Join-Path $skillsDir 'frontend\motion-web\SKILL.md') = 'frontend'
    (Join-Path $skillsDir 'backend\api-security\SKILL.md') = 'backend'
    (Join-Path $skillsDir 'workflow\openspec-apply-change\SKILL.md') = 'uncommon'
  }
  translations = [ordered]@{}
  api = [ordered]@{
    protocol = 'openai'
    baseUrl = 'https://api.openai.com/v1'
    model = 'gpt-4o-mini'
    apiKeyCipher = ''
  }
  ui = [ordered]@{ pinned = $false; allCollapsed = $false; collapsedCategories = @() }
}
if ($SeedTranslation) {
  $translationPath = Join-Path $skillsDir 'frontend\ui-review\SKILL.md'
  $description = 'Review interface hierarchy, spacing, typography, and interaction polish.'
  $hashBytes = [Security.Cryptography.SHA256]::HashData(
    [Text.Encoding]::UTF8.GetBytes($description)
  )
  $sourceHash = -join ($hashBytes | ForEach-Object { $_.ToString('x2') })
  $config.translations[$translationPath] = [ordered]@{
    sourceHash = $sourceHash
    text = '检查界面层级、间距、排版与交互细节。'
    updatedAt = '2026-10-18T00:00:00Z'
  }
}
$config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $appDir 'config.json') -Encoding utf8

$process = Start-Process -FilePath (Join-Path $appDir 'SkillBox.exe') `
  -WorkingDirectory $env:TEMP -PassThru
Start-Sleep -Seconds 2
$process.Refresh()
if ($process.HasExited) {
  throw "E2E app exited with code $($process.ExitCode)"
}
Write-Output $process.Id
