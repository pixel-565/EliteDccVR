@echo off
setlocal
cd /d "%~dp0"
call "..\..\SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 system_acceleration_test.cpp /Fe:system_acceleration_test.exe
if errorlevel 1 exit /b 1
system_acceleration_test.exe
exit /b %errorlevel%
