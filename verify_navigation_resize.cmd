@echo off
setlocal
cd /d "%~dp0"
call "%~dp0SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0InitializeSettings.ps1"
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /I vendor\openvr /I vendor\webview2 integration\verify_navigation_resize.cpp /Febin\VerifyNavigationResize.exe /Fobin\verify_navigation_resize.obj /link /SUBSYSTEM:CONSOLE vendor\openvr\openvr_api.lib d3d11.lib dxgi.lib d2d1.lib dwrite.lib user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib oleaut32.lib windowscodecs.lib dcomp.lib version.lib shlwapi.lib vendor\webview2\WebView2LoaderStatic.lib
if errorlevel 1 exit /b 1
bin\VerifyNavigationResize.exe
exit /b %errorlevel%
