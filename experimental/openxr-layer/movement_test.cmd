@echo off
setlocal
cd /d "%~dp0"
call "..\..\SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT movement_test.cpp /Fe:movement_test.exe
if errorlevel 1 exit /b 1
movement_test.exe
exit /b %errorlevel%
