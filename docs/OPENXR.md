# OpenXR setup and usage

## Tested setup

The 0.80.0 test build follows the user-tested Quest 3 / Quest Touch, Virtual Desktop VDXR and EDVR path (EDVR 0.22.0-rc.6 during development). Elite must already work through OpenXR. The layer uses D3D11; a native OpenVR-only launch will not load it. Other runtime/hardware combinations need testing. Legacy SteamVR instructions are separate.

## Install, update and disable

Extract the whole release ZIP, keeping `EliteDccVRLauncher.exe` next to the `OpenXR` directory. That directory contains the manifest, DLL, built-in Home assets and optional setup/diagnostic scripts.

Close Elite. Choose OpenXR in the launcher, browse to `OpenXR/EliteDccVR.OpenXRLayer.json`, and click Enable OpenXR. Start Elite in VR. The launcher displays the active Windows OpenXR runtime; choose the runtime in Virtual Desktop/SteamVR as applicable before starting. Enabling the layer does not switch runtimes or install a VR patch.

Registration is per-user under `HKCU\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit`. Previous EliteDccVR manifest registrations are removed; unrelated layers are preserved. Restart Elite for each new manifest/DLL. To uninstall, close Elite, click Disable OpenXR, then remove the package. Closing the launcher does not disable the layer. Implicit registration can affect other OpenXR processes, so disable it when not using/testing it.

Launcher preferences are in `%LOCALAPPDATA%\EliteDccVR\DualLauncher\launcher.ini`. The optional OpenVR backend expects a compatible plugin DLL; it does not launch the legacy 2.31.0 EXE package. Use that package's own launcher for the legacy edition.

## Panels

Toolbox toggles Keyboard, Browser and Settings. B/Y hides/restores the workspace without discarding browser tabs. Panels retain independent positions: grip moves a panel, grip plus vertical stick changes distance. Each panel has its own close/hide control. Toolbox Hide collapses it to a Show button.

Browser layout: navigation controls, tab row with add/close buttons, address bar, page. Home opens editable shortcuts. Copy/Paste follows the focused address field, page editor or supported dialog; a long-trigger popup is also available. Keyboard text goes to the focused browser field. Browser pages survive hiding. Resize changes viewport dimensions rather than stretching controls. Opacity/curvature affect the panel; page zoom affects content.

Settings tabs: Environment (theme, inactivity timeout), Ray (controller hand, thickness, tilt, sensitivity), Game Control (Integration, optional on-foot controls, map navigation), and currently inactive Exobiology placeholders.

## Native game interaction

Enable Integration in Game Control. Tested screens include main/options menus, maps, station services/subpages, cockpit service buttons, on-foot service dialogs and terminals. Unsupported/new layouts can require further inspection. Direct ray selection feeds native game UI and does not move the Windows cursor.

Galaxy Map: aim at a star, then use a short trigger press. Native UI controls retain priority; certified empty background can be passed through. System Map supports inspected stars, bodies, stations, carrier markers/stacks and Research Labs. A carrier stack is one target at its group icon; the game retains its own list/selection behavior.

An overview containing several bodies helps initial System Map registration. Sparse or unrecognized render variants may prevent initial matching. Unknown executable builds are intentionally refused by Integration; review compatibility after Elite updates.

## System Map joystick

The hand selected in Ray settings supplies stick X/Y pan. Grip switches vertical stick input to zoom; horizontal input is ignored while zooming. Own-panel interaction, dragging, resizing and held trigger take priority. Tracking loss, inactive input, lost focus and stale samples stop the added input.

Game Control saves:

- System Map pan speed: 1-100%, 1% steps.
- System Map zoom speed: 1-100%, 1% steps.
- Pan acceleration: 0-5 seconds, 0.1-second steps; default 1.5 seconds.

100% is the original native response. Pan starts at 5% of its scaled analog output and smoothly reaches the percentage-limited maximum. Neutral stops it immediately. Direction changes over 45 degrees, input gaps, hand changes, grip zoom and focus loss reset the ramp. Zero acceleration restores immediate response; zoom does not use this ramp.

Joystick activity follows the game's normal gamepad path to close dismissible map menus and resume the camera. Genuine modal dialogs keep their normal blocking behavior. Map navigation does not rewrite HOTAS bindings or camera transforms.

## Optional on-foot movement and Interact

1. Close Elite and run `OpenXR/Prepare-OnFootBindings.ps1`, for example:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File .\OpenXR\Prepare-OnFootBindings.ps1
   ```

2. Start Elite and select **EliteDccVR-OnFoot** only in **On Foot Controls**. The script clones the selected on-foot preset, leaving the original and StartPreset file unchanged.
3. Enable On-foot movement in Game Control. The selected hand provides forward/back and turning; the other stick provides strafing. Both dead zones are adjustable.
4. Trigger Interact is independent of movement enable. Outside an owned panel interaction it acts on the target selected by the game's view; it is not a separate physical-object raycast.

The native virtual gamepad bridge requires a free controller slot. If the preset is initially absent, back out of Controls and reopen it after bridge registration. Map joystick operation does not require this preset. A physical controller attached to the reserved slot takes priority.

## Local data and troubleshooting

Preferences and positions use `%LOCALAPPDATA%` files with the `EliteDccVR-OpenXR-` prefix; browser data is stored locally there as well. The on-foot status reader uses `%USERPROFILE%\Saved Games\Frontier Developments\Elite Dangerous\Status.json`.

The log is `%TEMP%\EliteDccVR-OpenXR-panel.log`; `OpenXR/Show-Diagnostics.cmd` opens diagnostics. Review local paths/game context before sharing logs. No separate diagnostic injector is needed.

- No panel: verify OpenXR rather than OpenVR launch, D3D11, selected runtime, enabled manifest and an Elite restart. Try B/Y and inspect session/layer initialization in the log.
- Browser absent: install/repair WebView2 Runtime and retain the Home directory next to the DLL.
- Integration unavailable: verify its switch and executable/signature checks. A game update can require an integration update; ordinary panels can be used without it.
- Map input blocked: avoid our panels/held trigger and close genuine modal dialogs. Dismissible menus should release through joystick activity.
- Saved panel outside view: with Elite closed, back up and remove only that panel's `EliteDccVR-OpenXR-*.position` file to restore default placement.

Do not post browser profiles, tokens, commander journals or raw memory dumps in public issues.

## Portable launcher paths

The release ZIP contains the layer manifest and DLL together in `OpenXR/`. Select the JSON manifest, not the DLL. On the first run from a new or moved package, the launcher selects its bundled manifest relative to its own EXE, instead of reusing another installation's saved absolute path. Explicitly selected external builds are remembered for the same launcher location while valid. GitHub source archives contain source code; download the Windows x64 release ZIP for prebuilt binaries.
