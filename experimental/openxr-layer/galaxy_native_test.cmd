@echo off
setlocal
cd /d "%~dp0"
call "..\..\SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /I "..\..\vendor\minhook\include" galaxy_native_test.cpp /Fegalaxy_native_test.exe /link nativeobj\buffer.obj nativeobj\hook.obj nativeobj\trampoline.obj nativeobj\hde64.obj
if errorlevel 1 exit /b 1
galaxy_native_test.exe
exit /b %errorlevel%
