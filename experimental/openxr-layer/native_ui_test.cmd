@echo off
setlocal
cd /d "%~dp0"
call "..\..\SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 native_ui_test.cpp /Fenative_ui_test.exe
if errorlevel 1 exit /b 1
native_ui_test.exe
exit /b %errorlevel%
