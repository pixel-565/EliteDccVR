# EliteDccVR

**Elite Dangerous Cockpit Companion** Ă˘â‚¬â€ť a Windows SteamVR overlay for Elite Dangerous.

EliteDccVR brings a ray-operated keyboard, browser, navigation and exobiology information into VR. Optional game integration detects Elite's interface geometry so the controller ray can drive the game's mouse and select supported cockpit controls.

[Download the Windows release](https://github.com/pixel-565/EliteDccVR/releases/latest) | [Source and issues](https://github.com/pixel-565/EliteDccVR)

The current version is **2.31.0**. This is a community project, not an official Frontier Developments product.

## Requirements

- Windows x64.
- SteamVR and Elite Dangerous running through SteamVR / OpenVR.
- A tracked controller. The supplied bindings have been tested with Quest 3 and Quest Touch through Virtual Desktop configured to use SteamVR.
- Microsoft Edge WebView2 Runtime for the browser.
- Keyboard bindings for Elite's **UI Up, UI Down, UI Left, UI Right and UI Panel Select**. The launcher reads the selected General Controls preset automatically. Arrow keys and Enter are a suitable starting configuration.

VDXR and native OpenXR overlay support are not implemented. The tested path is SteamVR even if OpenXR is also installed.

## Start

1. Download and extract the Windows x64 release, or build from source below. Keep the directory writable.
2. Start SteamVR and connect the headset/controllers. Start Elite using the SteamVR path.
3. Run `Launch.cmd`. It initializes settings without overwriting existing files, reads Elite's keyboard bindings, and starts `bin/EliteDccVR.exe`.
4. Press **B** on the right controller (or **Y** when using the left controller) to show the toolbox.
5. Open Keyboard, Browser, Game UI or Settings from the toolbox.

For a first run, hold B/Y for two seconds while the workspace is shown to place the panels around the ray. Previously saved positions are relative to the tracking space and may not suit another playspace.

The supplied preset enables the currently tested settings, including integration if enabled in `defaults/mouse-settings.txt`. You can disable Integration and Mouse in Settings / Game Control.

## Controller controls

| Input | Behavior |
| --- | --- |
| Short B/Y | Hide or restore the workspace, retaining visible panels and positions |
| Hold B/Y for two seconds while shown | Recenter the workspace; a progress bar indicates the hold |
| Ray + trigger | Select an overlay control; confirm a supported cockpit selection |
| Ray + grip over an overlay | Move that panel |
| Grip + joystick up/down while moving a panel | Change panel distance |
| Joystick up/down over detected game UI | Emulate mouse wheel scrolling |
| Trigger over detected game mouse UI | Left mouse button |
| Grip over detected game mouse UI | Right mouse button |
| Bottom corners of supported panels + trigger | Resize the panel |

Game Control has independent left/right drag divisors. Larger values slow dragging; `1` is full speed. Auto-align pauses its observations during dragging. The ray's sensitivity, thickness, tilt and inactivity timeout are configurable in Settings.

## Panels

- **Keyboard:** letters, numbers, Shift, Space, Enter, Backspace and Paste; configurable curvature and opacity. Keys can snap the ray to their centers.
- **Browser:** tabs, address bar, Home shortcuts, back/forward/reload, page zoom, Copy/Paste, context popup, file picking, scrolling, resizing, curvature and opacity. The Home page starts with Inara, Galmap and EDSM links. Browser contents retain each website's own theme.
- **Game UI:** current system and status, route systems and remaining distance from `NavRoute.json`, scoopability, body/planet information and exobiology progress. Active sample coordinates persist across restarts; distances use the current `Status.json` position and planet radius.
- **Settings:** Environment, Ray and Game Control tabs, including optional interface integration and diagnostic frames.

## Optional game integration

Integration loads a D3D11/OpenVR bridge into the running Elite process. It reads known UI draw geometry and cursor feedback; it does not replace game DLLs or modify EDVR files. The host can attach to a running game and reconnect after restarting the companion when protocols are compatible.

For menu and Galaxy Map mouse UI, enable **Integration**, **Mouse** and optionally **Auto-align cursor**. **Show frame** and **Cursor diagnostics** help verify the captured surface and cursor. Optional **Hide game cursor** is off by default. It hides only the cursor color output while preserving the original draw, depth processing and occlusion queries, so game interaction and cursor feedback continue.

When upgrading from 2.28.0, restart Elite once to load the updated bridge protocol.

Supported cockpit controls currently comprise **Starport Services, Auto Launch, Disembark and the four service icons above them**. These controls use Elite's UI navigation bindings rather than mouse input. Aiming changes the selection; trigger requests confirmation, which is sent only after the observed highlight matches the target. Unknown or ambiguous highlights are not confirmed.

Detection uses fingerprints from tested game rendering paths. A game update or a different cockpit layout can require a new profile. After a bridge protocol change, restart Elite once. Close other VRKeyboard/EliteDccVR instances before launching; the historical internal application and IPC identifiers are intentionally retained for compatibility.

## Settings and local data

`defaults/` contains the tested starting preset. `InitializeSettings.ps1` copies missing files to the application directory. Existing settings are preserved. Runtime files are ignored by Git.

- `journal-settings.json`: `HistoryDays` defaults to 20. An empty `JournalFolder` uses Windows' Saved Games folder followed by `Frontier Developments/Elite Dangerous`. A custom absolute path may be supplied locally.
- `ExobiSetting.json`: exobiology row and distance colors, row spacing and `StatusReadIntervalMs` (100 ms by default during active sampling).
- `anchor-interval.txt`: UI anchoring confirmation interval in milliseconds; editable if needed.
- Other setting files store panel positions, theme, ray settings, mouse sensitivities and drag divisors.
- `browser-profile/`: local WebView2 cookies, history and Home shortcut edits.
- `exobi-samples.json`: active sample positions; not distributed or tracked.
- `logs/`: local troubleshooting logs; not distributed or tracked.

The repository and release do not include commander journals, personal browser data, RenderDoc captures or extracted game shaders/images. Browser networking is handled by WebView2 and the websites you choose. Journal-derived panels read local data; they do not upload commander history.

## Build from source

Install Visual Studio 2022 or newer, or Visual Studio Build Tools, with **Desktop development with C++** and a Windows SDK. The scripts discover the x64 toolchain using `vswhere`; no user-specific installation path is required.

```bat
build.cmd
verify.cmd
verify_navigation.cmd
verify_navigation_resize.cmd
```

Vendored dependencies allow building without downloading packages. `build.cmd` creates the app, the integration host/bridge and copies the OpenVR runtime library into `bin/`.

Core, navigation, exobiology and panel-layout tests are self-contained. Tests requiring private RenderDoc fixtures are compiled but explicitly skipped when those fixtures are absent. Game shader bytecode and extracted images are not shipped as test assets. Live headset testing remains necessary for geometry alignment and controller behavior.

To produce a Windows release ZIP after building:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Package.ps1
```

## Dependencies and attribution

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the retained license files under `vendor/`. Exobiology values and minimum sample distances are derived from the pinned EDDI reference data; no EDDI application code is used at runtime.

## License

The project's own code is licensed under the [MIT License](LICENSE). Third-party components and derived data retain their respective licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
