@echo off
if defined VSCMD_VER exit /b 0
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  echo Visual Studio Build Tools with Desktop development with C++ is required.
  exit /b 1
)
set "APP_VS_INSTALL="
for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "APP_VS_INSTALL=%%I"
if not defined APP_VS_INSTALL (
  echo No Visual Studio C++ x64 build tools installation was found.
  exit /b 1
)
call "%APP_VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
exit /b %errorlevel%
