@echo off
setlocal
cd /d "%~dp0"
call "..\..\SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
call build-native.cmd
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /LD /I "..\..\vendor\webview2" layer.cpp /Fe:EliteDccVR.OpenXRLayer.dll /link nativeobj\NativeUI.lib d3d11.lib d3dcompiler.lib dxgi.lib d2d1.lib dwrite.lib gdi32.lib user32.lib ole32.lib oleaut32.lib dcomp.lib windowscodecs.lib version.lib shlwapi.lib "..\..\vendor\webview2\WebView2LoaderStatic.lib"
exit /b %errorlevel%
