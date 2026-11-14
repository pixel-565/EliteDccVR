# EliteDccVR

**Elite Dangerous Cockpit Companion** - controller-operated VR panels and native game-interface interaction for Windows.

## Choose your edition

| Edition | Runtime | Download / guide |
| --- | --- | --- |
| OpenXR 0.80.1 | OpenXR / D3D11; tested with Quest 3, Quest Touch, Virtual Desktop VDXR and EDVR | [OpenXR test release](https://github.com/pixel-565/EliteDccVR/releases/tag/openxr-v0.80.1) |
| Legacy 2.31.0 | SteamVR / OpenVR | [SteamVR release](https://github.com/pixel-565/EliteDccVR/releases/tag/v2.31.0), [legacy guide](docs/STEAMVR.md) |

The OpenXR edition is an experimental public test build. Its panels, native selection, System Map navigation and adjustable acceleration have been tested in the developer's headset setup. Other hardware, runtimes and game versions are not comprehensively validated. The original SteamVR edition remains available separately.

## OpenXR features

- Independent Toolbox, Browser, Keyboard and Settings panels with saved positions, controller-ray input and an endpoint marker.
- Browser tabs, address bar, editable Home shortcuts, navigation icons, page zoom, text selection, Copy/Paste, file picking, scrolling, resizing, curvature and opacity. Websites retain their own colors; the built-in Home follows the interface theme.
- VR keyboard with letters, numbers, Shift, Space, Enter, Backspace and Paste; curvature, opacity and optional key-center snapping.
- Environment themes, selectable left/right controller, ray thickness/tilt/sensitivity and inactivity timeout.
- Optional native Elite UI integration for supported menus, maps, station services, cockpit service controls, on-foot dialogs and terminals. Selection uses native input rather than OS mouse movement.
- Direct Galaxy Map star selection and System Map body/station/carrier selection, including validated carrier stacks and Research Labs markers. Actual UI controls retain priority over targets behind transparent map areas.
- System Map joystick pan and grip-plus-stick zoom; saved speed percentages and adjustable pan acceleration.
- Optional analog on-foot movement with both controllers and separate dead zones, plus trigger Interact on the game's view-selected contextual target.

Journal-based Navigation/planet/exobiology panels and biological-object detection from the legacy edition are **not yet ported to OpenXR**. Unfinished controls are placeholders, not implemented features.

## Quick start: OpenXR

1. Make sure Elite already works in VR through **OpenXR**. The tested configuration uses EDVR with Virtual Desktop set to **VDXR**. This package does not install EDVR or convert an OpenVR game by itself.
2. Download the Windows x64 ZIP from the [OpenXR release](https://github.com/pixel-565/EliteDccVR/releases/tag/openxr-v0.80.1), extract it to a permanent writable folder, and close Elite.
3. Run `EliteDccVRLauncher.exe`, choose **OpenXR**, and select `OpenXR/EliteDccVR.OpenXRLayer.json` with **Browse** if necessary.
4. Click **Enable OpenXR**, then start Elite in VR. The optional Steam checkbox can launch Elite; the game must still be configured for OpenXR.
5. **B** on the right controller or **Y** on the left hides/shows the workspace. Open Browser, Keyboard or Settings from the Toolbox.
6. Enable **Integration** in **Settings > Game Control** for game interaction. Optional on-foot movement requires the separate preset preparation in the [OpenXR guide](docs/OPENXR.md).

The launcher registers a per-user implicit OpenXR API layer. Registration persists after it closes. Use **Disable OpenXR** before removing or moving the package. Restart Elite after changing builds; closing the launcher alone cannot unload a running layer.

Requirements: Windows x64, a D3D11 OpenXR game path, tracked controllers, Microsoft Edge WebView2 Runtime, and the Microsoft Visual C++ x64 runtime. The layer is tested with Elite; support for other OpenXR applications is not claimed.

## Controls

| Input | OpenXR behavior |
| --- | --- |
| B / Y | Hide or restore the workspace |
| Ray + trigger | Click an overlay control or select a supported game target |
| Grip over our panel | Move that panel |
| Grip + vertical stick while moving a panel | Change panel distance |
| Trigger on a browser bottom corner | Resize the browser |
| Vertical stick over the browser page | Scroll |
| Stick in System Map | Analog pan, unless an overlay interaction owns input |
| Grip + vertical stick in System Map | Zoom in/out |
| Toolbox Hide / Show | Collapse the Toolbox / expand it |

System Map pan starts slowly and accelerates while held. Neutral stops the added motion immediately; substantial direction changes restart the ramp. The saved percentage limits maximum speed; **Pan acceleration = 0** restores immediate response. Native gamepad activity releases dismissible map menus, while real modal dialogs retain the game's blocking behavior.

[Full setup, controls and troubleshooting](docs/OPENXR.md) | [Build from source](docs/BUILD.md) | [Legacy SteamVR](docs/STEAMVR.md)

## Architecture and limitations

`experimental/shared-vr-ui/` contains shared panel/browser behavior, settings and native integration. `experimental/openxr-layer/` provides OpenXR/D3D11 composition and controller routing. `experimental/launcher/` is the desktop launcher. Original SteamVR source remains at the repository root; the complete runtime merge is still in progress, so feature parity is not claimed.

Integration validates the tested Odyssey executable build and uses optional native hooks. A game update may require signature review; browser/keyboard use does not require successful game integration. Map matching has identity/freshness guards and needs enough unambiguous rendered anchors for initial registration. New marker/mesh variants may need additional testing.

No EDVR files are modified. Do not launch the legacy OpenVR overlay against a VDXR-only session.

## Privacy, licensing and contributions

This source/release excludes commander journals, browser profiles/history, personal Home shortcuts, saved sample coordinates, local settings, diagnostic dumps, RenderDoc captures and extracted game assets. See the OpenXR guide for local data paths. Inspect logs before sharing them in issues.

Own code: [MIT](LICENSE). Dependencies retain their [licenses and notices](THIRD_PARTY_NOTICES.md). Elite Dangerous and its assets belong to their owners; this is an unofficial community project.

See [CONTRIBUTING.md](CONTRIBUTING.md). Do not report offline tests as headset verification.
