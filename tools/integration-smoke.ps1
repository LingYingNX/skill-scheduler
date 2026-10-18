param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$YamlDll
)

$ErrorActionPreference = 'Stop'
$sourceExe = (Resolve-Path -LiteralPath $Exe).Path
$sourceDll = (Resolve-Path -LiteralPath $YamlDll).Path
$projectRoot = Split-Path -Parent $PSScriptRoot
$workRoot = Join-Path $projectRoot 'build\integration-smoke'

if (Test-Path -LiteralPath $workRoot) {
  Remove-Item -LiteralPath $workRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $workRoot | Out-Null

$appDir = Join-Path $workRoot 'app'
$skillsDir = Join-Path $workRoot 'skills'
$skillDir = Join-Path $skillsDir 'nested\demo'
New-Item -ItemType Directory -Path $appDir | Out-Null
New-Item -ItemType Directory -Path $skillDir | Out-Null
Copy-Item -LiteralPath $sourceExe -Destination (Join-Path $appDir 'SkillBox.exe')
Copy-Item -LiteralPath $sourceDll -Destination (Join-Path $appDir (Split-Path -Leaf $sourceDll))
Copy-Item -LiteralPath (Join-Path $projectRoot 'web') -Destination $appDir -Recurse

$skillFile = Join-Path $skillDir 'SKILL.md'
@'
---
name: integration-demo
description: English description for integration testing.
---
'@ | Set-Content -LiteralPath $skillFile -Encoding utf8

$configPath = Join-Path $appDir 'config.json'
$config = [ordered]@{
  version = 1
  paths = @($skillsDir)
  categories = [ordered]@{ $skillFile = 'favorite' }
  translations = [ordered]@{
    $skillFile = [ordered]@{
      sourceHash = 'stale-hash'
      text = '集成测试译文'
      updatedAt = '2026-10-18T00:00:00Z'
    }
  }
  api = [ordered]@{
    protocol = 'openai'
    baseUrl = 'https://api.openai.com/v1'
    model = 'gpt-4o-mini'
    apiKeyCipher = ''
  }
  ui = [ordered]@{ pinned = $false; allCollapsed = $false; collapsedCategories = @() }
}
$config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $configPath -Encoding utf8

function Start-SmokeApp {
  $process = Start-Process -FilePath (Join-Path $appDir 'SkillBox.exe') -WorkingDirectory $env:TEMP -PassThru
  Start-Sleep -Seconds 2
  $process.Refresh()
  if ($process.HasExited) {
    throw "SkillBox exited during integration smoke test with code $($process.ExitCode)"
  }
  return $process
}

function Stop-SmokeApp($process) {
  if ($process -and -not $process.HasExited) {
    Stop-Process -Id $process.Id -Force
    Wait-Process -Id $process.Id -ErrorAction SilentlyContinue
  }
}

$process = Start-SmokeApp
if (-not (Test-Path -LiteralPath $configPath)) {
  Stop-SmokeApp $process
  throw 'config.json was not created beside the executable'
}

Remove-Item -LiteralPath $skillFile -Force
Start-Sleep -Milliseconds 2500
Stop-SmokeApp $process

$deletedConfig = Get-Content -LiteralPath $configPath -Raw -Encoding utf8 | ConvertFrom-Json
if (@($deletedConfig.translations.PSObject.Properties).Count -ne 0) {
  throw 'translation cache was not removed after SKILL.md deletion'
}

$missingRoot = Join-Path $workRoot 'offline-root'
$offlineSkill = Join-Path $missingRoot 'cached\SKILL.md'
$offlineConfig = [ordered]@{
  version = 1
  paths = @($missingRoot)
  categories = [ordered]@{ $offlineSkill = 'backend' }
  translations = [ordered]@{
    $offlineSkill = [ordered]@{
      sourceHash = 'offline-hash'
      text = '离线译文'
      updatedAt = '2026-10-18T00:00:00Z'
    }
  }
  api = [ordered]@{
    protocol = 'openai'
    baseUrl = 'https://api.openai.com/v1'
    model = 'gpt-4o-mini'
    apiKeyCipher = ''
  }
  ui = [ordered]@{ pinned = $false; allCollapsed = $false; collapsedCategories = @() }
}
$offlineConfig | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $configPath -Encoding utf8

$process = Start-SmokeApp
Start-Sleep -Seconds 1
Stop-SmokeApp $process
$offlineResult = Get-Content -LiteralPath $configPath -Raw -Encoding utf8 | ConvertFrom-Json
if ($offlineResult.paths[0] -ne $missingRoot) {
  throw 'unavailable root path was not preserved'
}
if (-not $offlineResult.translations.PSObject.Properties[$offlineSkill]) {
  throw 'translation for unavailable root was removed'
}

'{ invalid json' | Set-Content -LiteralPath $configPath -Encoding utf8
$process = Start-SmokeApp
Start-Sleep -Seconds 1
Stop-SmokeApp $process
$backups = Get-ChildItem -LiteralPath $appDir -Filter 'config.json.corrupt-*'
if (-not $backups) {
  throw 'corrupt config backup was not created'
}
$recovered = Get-Content -LiteralPath $configPath -Raw -Encoding utf8 | ConvertFrom-Json
if ($recovered.version -ne 1) {
  throw 'recovered config is not valid'
}

Write-Output 'Integration smoke passed: watcher deletion, offline root, exe-directory config, corruption recovery.'
