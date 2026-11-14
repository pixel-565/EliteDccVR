@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"
for %%T in (system_acceleration_test system_native_test movement_controller_test toolbox_test native_ui_test panel_hit_test galaxy_surface_test native_drag_test native_owner_lifetime_test) do (
 call %%T.cmd
 if not "!errorlevel!"=="0" exit /b 1
)
exit /b 0
