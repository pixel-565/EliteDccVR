@echo off
setlocal
cd /d "%~dp0"
call "..\..\SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT native_gamepad_test.cpp /Fe:native_gamepad_test.exe
if errorlevel 1 exit /b 1
native_gamepad_test.exe
exit /b %errorlevel%
