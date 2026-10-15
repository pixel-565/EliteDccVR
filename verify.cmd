@echo off
setlocal
cd /d "%~dp0"
call "%~dp0SetupBuildEnv.cmd"
if errorlevel 1 exit /b 1
if not exist bin mkdir bin
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT integration\verify_ship.cpp /Febin\VerifyShip.exe /Fobin\verify_ship.obj /link bcrypt.lib
if errorlevel 1 exit /b 1
bin\VerifyShip.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT integration\verify_game_wheel.cpp /Febin\VerifyGameWheel.exe /Fobin\verify_game_wheel.obj /link user32.lib
if errorlevel 1 exit /b 1
bin\VerifyGameWheel.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT integration\verify_late_discovery.cpp /Febin\VerifyLateDiscovery.exe /Fobin\verify_late_discovery.obj
if errorlevel 1 exit /b 1
bin\VerifyLateDiscovery.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT integration\verify_cursor_signature.cpp /Febin\VerifyCursorSignature.exe /Fobin\verify_cursor_signature.obj /link bcrypt.lib
if errorlevel 1 exit /b 1
if exist "integration\fixtures\cursor-vs.dxbc" (
  bin\VerifyCursorSignature.exe
  if errorlevel 1 exit /b 1
) else (
  echo SKIP VerifyCursorSignature: private game capture fixtures are not distributed.
)
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /I vendor\minhook\include integration\verify_ship_gpu.cpp obj\buffer.obj obj\hook.obj obj\trampoline.obj obj\hde64.obj /Fobin\verify_ship_gpu.obj /Febin\VerifyShipGpu.exe /link d3d11.lib bcrypt.lib user32.lib
if errorlevel 1 exit /b 1
if exist "integration\fixtures\ship\panel.rgba" (
  bin\VerifyShipGpu.exe
  if errorlevel 1 exit /b 1
) else (
  echo SKIP VerifyShipGpu: private game capture fixtures are not distributed.
)
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /I vendor\minhook\include integration\verify.cpp obj\buffer.obj obj\hook.obj obj\trampoline.obj obj\hde64.obj /Fobin\verify.obj /Febin\VerifyUi.exe /link d3d11.lib bcrypt.lib user32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /I vendor\openvr integration\verify_pose.cpp /Fobin\verify_pose.obj /Febin\VerifyPose.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT integration\verify_cursor.cpp /Fobin\verify_cursor.obj /Febin\VerifyCursor.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT integration\verify_align.cpp /Fobin\verify_align.obj /Febin\VerifyAlign.exe
if errorlevel 1 exit /b 1
bin\VerifyAlign.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT integration\verify_settings.cpp /Fobin\verify_settings.obj /Febin\VerifySettings.exe
if errorlevel 1 exit /b 1
bin\VerifySettings.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT integration\verify_ray_smoothing.cpp /Fobin\verify_ray_smoothing.obj /Febin\VerifyRaySmoothing.exe
if errorlevel 1 exit /b 1
bin\VerifyRaySmoothing.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /MT /I vendor\openvr integration\verify_controller_idle.cpp /Fobin\verify_controller_idle.obj /Febin\VerifyControllerIdle.exe
if errorlevel 1 exit /b 1
bin\VerifyControllerIdle.exe
if errorlevel 1 exit /b 1
bin\VerifyPose.exe
exit /b %errorlevel%
