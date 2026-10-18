@echo off
setlocal
set "APP=%~dp0build\windows-x64\Release\SkillBox.exe"

if not exist "%APP%" (
  echo SkillBox.exe not found.
  echo Build the Release target first: %APP%
  pause
  exit /b 1
)

start "" /D "%~dp0build\windows-x64\Release" "%APP%"
