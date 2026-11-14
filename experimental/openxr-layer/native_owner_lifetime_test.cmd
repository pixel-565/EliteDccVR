@echo off
setlocal
cd /d "%~dp0"
call "..\..\SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /I "..\..\vendor\minhook\include" native_owner_lifetime_test.cpp nativeobj\galaxy.obj nativeobj\system_map.obj nativeobj\buffer.obj nativeobj\hook.obj nativeobj\trampoline.obj nativeobj\hde64.obj /Fe:native_owner_lifetime_test.exe
if errorlevel 1 exit /b 1
native_owner_lifetime_test.exe
exit /b %errorlevel%
