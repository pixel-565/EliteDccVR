@echo off
setlocal
cd /d "%~dp0"
call "..\..\SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
set "MH=..\..\vendor\minhook"
if not exist nativeobj mkdir nativeobj
cl /nologo /c /O2 /MT /I "%MH%\include" /Fonativeobj\ "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde64.c"
if errorlevel 1 exit /b 1
cl /nologo /c /std:c++20 /EHsc /W4 /O2 /MT /I "%MH%\include" /Fonativeobj\ "..\shared-vr-ui\game-native\native_ui.cpp" "..\shared-vr-ui\game-native\movement.cpp" "..\shared-vr-ui\game-native\galaxy.cpp" "..\shared-vr-ui\game-native\system_map.cpp"
if errorlevel 1 exit /b 1
lib /nologo /out:nativeobj\NativeUI.lib nativeobj\native_ui.obj nativeobj\movement.obj nativeobj\galaxy.obj nativeobj\system_map.obj nativeobj\buffer.obj nativeobj\hook.obj nativeobj\trampoline.obj nativeobj\hde64.obj
exit /b %errorlevel%
