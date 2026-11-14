@echo off
setlocal
cd /d "%~dp0"
call "..\..\SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 galaxy_surface_test.cpp /Fegalaxy_surface_test.exe /link nativeobj\NativeUI.lib d3d11.lib bcrypt.lib
if errorlevel 1 exit /b 1
galaxy_surface_test.exe
exit /b %errorlevel%
