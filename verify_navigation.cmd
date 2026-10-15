@echo off
setlocal
cd /d "%~dp0"
call "%~dp0SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0InitializeSettings.ps1"
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT integration\verify_navigation.cpp /Fobin\verify_navigation.obj /Febin\VerifyNavigation.exe /link shell32.lib ole32.lib
if errorlevel 1 exit /b 1
bin\VerifyNavigation.exe journal-settings.json
exit /b %errorlevel%
