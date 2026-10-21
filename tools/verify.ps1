param(
  [string]$Configuration = "Release",
  [switch]$SkipConfigure,
  [switch]$SkipPreview
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot "build\windows-x64"
$previewDir = Join-Path $projectRoot "tools\preview"

function Find-VisualStudioTool([string]$name) {
  $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path -LiteralPath $vswhere) {
    $vsRoot = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if ($vsRoot) {
      $candidate = Join-Path $vsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\$name.exe"
      if (Test-Path -LiteralPath $candidate) {
        return $candidate
      }
    }
  }
  $command = Get-Command $name -ErrorAction SilentlyContinue
  if ($command) {
    return $command.Source
  }
  throw "$name was not found. Install Visual Studio Build Tools or add it to PATH."
}

function Assert-LastExit([string]$step) {
  if ($LASTEXITCODE -ne 0) {
    throw "$step failed with exit code $LASTEXITCODE"
  }
}

$cmake = Find-VisualStudioTool "cmake"
$ctest = Find-VisualStudioTool "ctest"

if (-not $SkipConfigure) {
  & $cmake -S $projectRoot -B $buildDir -G "Visual Studio 17 2022" -A x64
  Assert-LastExit "cmake configure"
}

Write-Host "==> Build ($Configuration)"
& $cmake --build $buildDir --config $Configuration --parallel
Assert-LastExit "cmake build"

Write-Host "==> CTest"
& $ctest --test-dir $buildDir -C $Configuration --output-on-failure
Assert-LastExit "ctest"

Write-Host "==> Integration smoke"
$exe = Join-Path $buildDir "$Configuration\SkillBox.exe"
$yaml = Join-Path $buildDir "$Configuration\yaml-cpp.dll"
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "integration-smoke.ps1") -Exe $exe -YamlDll $yaml
Assert-LastExit "integration smoke"

if ($SkipPreview) {
  Write-Host "Preview checks skipped."
  exit 0
}

Write-Host "==> Preview checks"
$node = Get-Command node -ErrorAction SilentlyContinue
if (-not $node) {
  throw "node was not found. Install Node.js to run the preview checks."
}

$localPlaywright = Join-Path $previewDir "node_modules\playwright"
if (-not (Test-Path -LiteralPath $localPlaywright)) {
  $npm = Get-Command npm -ErrorAction SilentlyContinue
  if (-not $npm) {
    throw "playwright is not installed in tools\preview and npm was not found."
  }
  Write-Host "Installing pinned Playwright into tools\preview"
  Push-Location $previewDir
  try {
    & $npm.Source install --no-audit --no-fund
    Assert-LastExit "npm install"
  } finally {
    Pop-Location
  }
}

$previousNodePath = $env:NODE_PATH
$env:NODE_PATH = (Join-Path $previewDir "node_modules")
if ($previousNodePath) {
  $env:NODE_PATH = "$env:NODE_PATH;$previousNodePath"
}

$server = $null
try {
  $server = Start-Process -FilePath $node.Source `
    -ArgumentList (Join-Path $PSScriptRoot "preview\server.mjs") `
    -WorkingDirectory $projectRoot -PassThru -WindowStyle Hidden
  $ready = $false
  for ($attempt = 0; $attempt -lt 40; $attempt++) {
    Start-Sleep -Milliseconds 250
    try {
      Invoke-WebRequest -Uri "http://127.0.0.1:4173/" -UseBasicParsing -TimeoutSec 2 | Out-Null
      $ready = $true
      break
    } catch {
    }
  }
  if (-not $ready) {
    throw "preview server did not start on port 4173"
  }
  & $node.Source (Join-Path $PSScriptRoot "preview\capture.cjs")
  Assert-LastExit "preview checks"
} finally {
  if ($server -and -not $server.HasExited) {
    Stop-Process -Id $server.Id -Force
    Wait-Process -Id $server.Id -ErrorAction SilentlyContinue
  }
  $env:NODE_PATH = $previousNodePath
}

Write-Host "All checks passed."
