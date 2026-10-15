@echo off
setlocal
cd /d "%~dp0"
call "%~dp0SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
if not exist obj mkdir obj
cl /nologo /c /O2 /MT /I vendor\minhook\include /Foobj\ vendor\minhook\src\buffer.c vendor\minhook\src\hook.c vendor\minhook\src\trampoline.c vendor\minhook\src\hde\hde64.c
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /LD /I vendor\minhook\include integration\bridge.cpp obj\buffer.obj obj\hook.obj obj\trampoline.obj obj\hde64.obj /Fobin\bridge.obj /Febin\VRKeyboard.UiBridge.dll /link d3d11.lib bcrypt.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT integration\host.cpp /Fobin\host.obj /Febin\VRKeyboard.UiHost.exe /link user32.lib d3d11.lib dxgi.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /DUNICODE /D_UNICODE /I vendor\openvr /I vendor\webview2 main.cpp /Febin\EliteDccVR.exe /Fobin\main.obj /link /SUBSYSTEM:WINDOWS vendor\openvr\openvr_api.lib d3d11.lib dxgi.lib d2d1.lib dwrite.lib user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib oleaut32.lib windowscodecs.lib dcomp.lib version.lib shlwapi.lib vendor\webview2\WebView2LoaderStatic.lib
if errorlevel 1 exit /b 1
copy /y vendor\openvr\openvr_api.dll bin\openvr_api.dll >nul
exit /b 0

