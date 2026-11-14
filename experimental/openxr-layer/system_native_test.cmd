@echo off
setlocal
cd /d "%~dp0"
call "..\..\SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /I "..\..\vendor\minhook\include" system_native_test.cpp nativeobj\buffer.obj nativeobj\hook.obj nativeobj\trampoline.obj nativeobj\hde64.obj /Fe:system_native_test.exe
if errorlevel 1 exit /b 1
system_native_test.exe
exit /b %errorlevel%
