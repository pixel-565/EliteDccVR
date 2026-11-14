# Build from source

## Requirements

Windows x64; Visual Studio 2022/2026 or Build Tools with Desktop development with C++, a Windows SDK and C++/WinRT headers. `SetupBuildEnv.cmd` finds the compiler using vswhere. WebView2, JSON and MinHook are pinned in `vendor`; OpenXR headers/licenses are in `experimental/openxr-sdk`.

## OpenXR layer and launcher

From the repository root:

```bat
experimental\openxr-layer\build.cmd
experimental\launcher\build.cmd
```

Outputs: `experimental/openxr-layer/EliteDccVR.OpenXRLayer.dll` and `experimental/launcher/EliteDccVRLauncher.exe`. In the launcher browse to `experimental/openxr-layer/EliteDccVR.OpenXRLayer.json` when testing source builds. Keep the layer's `home` assets alongside it. Close and restart Elite after changing enabled builds.

The public ZIP ships the exact user-confirmed 0.80 development DLL. Source changes for publication only make includes/build paths portable; a rebuilt PE can differ in timestamp/hash. The launcher is rebuilt with package-relative defaults. Local profiles/settings are not build inputs.

## Portable regression tests

Build first, then run:

```bat
experimental\openxr-layer\system_acceleration_test.cmd
experimental\openxr-layer\system_native_test.cmd
experimental\openxr-layer\movement_controller_test.cmd
experimental\openxr-layer\toolbox_test.cmd
experimental\openxr-layer\native_ui_test.cmd
experimental\openxr-layer\panel_hit_test.cmd
experimental\openxr-layer\galaxy_surface_test.cmd
experimental\openxr-layer\native_drag_test.cmd
experimental\openxr-layer\native_owner_lifetime_test.cmd
```

These use synthetic memory/input and, where appropriate, D3D11 WARP. They do not require Elite and are not headset tests. Replay checks requiring RenderDoc captures, extracted game geometry/shaders or process dumps are intentionally excluded.

Launcher checks:

```bat
experimental\launcher\EliteDccVRLauncher.exe --self-test
experimental\launcher\EliteDccVRLauncher.exe --verify-layer experimental\openxr-layer\EliteDccVR.OpenXRLayer.json
```

Legacy SteamVR source remains at the repository root, with its original `build.cmd`, `verify.cmd`, `verify_navigation.cmd` and `verify_navigation_resize.cmd`.

## Layout

- Repository root: preserved legacy SteamVR application and licensed dependencies.
- `experimental/shared-vr-ui`: common panel/browser behavior, settings, native integration.
- `experimental/openxr-layer`: composition, controller routing and portable tests.
- `experimental/openxr-sdk/include/openxr`: pinned Khronos headers only.
- `experimental/launcher`: desktop backend/build selection and layer registration.

Do not commit generated binaries, settings/positions, browser profiles, commander data, credentials or extracted game assets. See CONTRIBUTING.md.
