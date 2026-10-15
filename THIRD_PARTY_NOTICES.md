# Third-party notices

This list applies to the bundled dependencies and reference data. Their licenses are separate from the license chosen for EliteDccVR's own code.

| Component | Upstream | Included terms |
| --- | --- | --- |
| OpenVR SDK and x64 runtime | https://github.com/ValveSoftware/openvr | `vendor/openvr/LICENSE` (BSD 3-Clause); pinned revision in `REVISION.txt` |
| MinHook 1.3.4 | https://github.com/TsudaKageyu/minhook | `vendor/minhook/LICENSE.txt`, including third-party disassembler notices |
| nlohmann/json 3.11.3 | https://github.com/nlohmann/json | `vendor/json/LICENSE.MIT` |
| Microsoft.Web.WebView2 SDK 1.0.4191.47 | https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4191.47 | `vendor/webview2/LICENSE.txt` and `NOTICE.txt`; headers and static loader from that package |
| EDDI exobiology reference data | https://github.com/EDCD/EDDI | Apache 2.0; `vendor/eddi/LICENSE.md`, `LicenseFile.txt` and `NOTICE.txt` |

EDDI source revision: `d3b964ea7c8bb959ad6537f55b308f293a326905`. `exobiology-catalog.json` contains a compact transformation of species values, English names and minimum sample distances from OrganicSpecies, OrganicGenus and their resources. `integration/update_exobiology_catalog.py` regenerates this data from the pinned revision. The file's metadata identifies the source and units.

The binary release includes these notices and all license texts. Elite Dangerous, its journals and game assets belong to their respective rights holders. No game shader bytecode, extracted game images or RenderDoc captures are distributed here. Runtime detection uses fingerprints and independently implemented geometry decoding.
