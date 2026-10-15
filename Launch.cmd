@echo off
setlocal
cd /d "%~dp0"
if not exist "bin\EliteDccVR.exe" (
  echo Build the application with build.cmd, or download a compiled release.
  pause
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0InitializeSettings.ps1"
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0ReadBindings.ps1"
if errorlevel 1 (
  pause
  exit /b 1
)
start "" bin\EliteDccVR.exe
