@echo off
setlocal
cd /d "%~dp0"
call "..\..\SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 panel_hit_test.cpp /Fepanel_hit_test.exe
if errorlevel 1 exit /b 1
panel_hit_test.exe
exit /b %errorlevel%
