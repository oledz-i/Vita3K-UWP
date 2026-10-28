<p align="center"><img src="docs/assets/banner.svg" alt="Vita3K UWP — A new home for Vita emulation" width="100%"></p>

<p align="center">
  <a href="https://github.com/oledz-i/Vita3K-UWP/actions/workflows/full-core-appx.yml"><img src="https://github.com/oledz-i/Vita3K-UWP/actions/workflows/full-core-appx.yml/badge.svg" alt="Full core build"></a>
  <img src="https://img.shields.io/badge/stage-alpha-f59e0b" alt="Alpha">
  <img src="https://img.shields.io/badge/target-Xbox%20Dev%20Mode-107c10" alt="Xbox Dev Mode target">
  <a href="COPYING.txt"><img src="https://img.shields.io/badge/license-GPL--2.0--or--later-blue" alt="GPL license"></a>
</p>

<p align="center"><b>An independent, experimental Vita3K port for Xbox Series X|S, built on UWP.</b><br>Native console UI. Open development. Built on the work of the Vita3K community.</p>

<p align="center"><a href="docs/INSTALL.md">Install</a> · <a href="docs/BUILDING.md">Build</a> · <a href="docs/UI.md">Frontend</a> · <a href="docs/COMPATIBILITY.md">Compatibility</a> · <a href="docs/ROADMAP.md">Roadmap</a> · <a href="docs/CORE_PORT.md">Core port</a> · <a href="docs/RENDERERS.md">Renderers</a> · <a href="CONTRIBUTING.md">Contribute</a></p>

<p align="center"><img src="docs/assets/screens/home.png" alt="Home screen of the Xbox frontend" width="100%"></p>

<table>
<tr>
<td width="50%"><img src="docs/assets/screens/library.png" alt="Library"></td>
<td width="50%"><img src="docs/assets/screens/settings.png" alt="Settings"></td>
</tr>
<tr>
<td width="50%"><img src="docs/assets/screens/xmb.png" alt="PS3-style XMB home screen"></td>
<td width="50%"><img src="docs/assets/screens/game.png" alt="Game page with backdrop art"></td>
</tr>
</table>
<p align="center"><sub>Rendered previews of the frontend (Cinematic home, Library, Settings, the optional PS3-style XMB, and a game page with backdrop art). Game names and art are placeholders.</sub></p>

## Where things stand

**Alpha.** Real Vita games now run on the Xbox Series S in Dev Mode, on two graphics paths: Vulkan through Mesa Dozen, and OpenGL through Mesa on Direct3D 12. Compatibility is uneven and performance is still being tuned, so expect rough edges, and please send logs.

| Area | Status |
| --- | --- |
| UWP app and packaging | Signed x64 APPX built by GitHub Actions |
| Vita3K core | Full libretro core for UWP (WizzardSK/vita3k-libretro, pinned, merged with current upstream in CI) |
| Graphics | **Vulkan** (Mesa Dozen on D3D12, with our patches) and **OpenGL** (Mesa on D3D12). Pick per game |
| CPU | Dynarmic with a UWP W^X patch |
| Firmware | Installs on Xbox from the app (system firmware 3.74 confirmed) |
| Games tested | Terraria, Undertale, Persona 4 Golden, Gravity Rush, Danganronpa: Trigger Happy Havoc, Ultra Despair Girls, Minecraft: PS Vita Edition (Vulkan). See [compatibility](docs/COMPATIBILITY.md) |
| Frontend | Cinematic home or **PS3-style XMB**, Library, game pages with backdrops, five themes, original music and sounds |
| Artwork | Covers and backdrops from **SteamGridDB** with an in-app picker, or your own image; screenshot backdrops |
| Settings | Firmware, storage (data on a drive), 24 Vita3K emulation options, appearance, diagnostics |
| Input | Controller, plus a stick-driven touch cursor for games that need the touch screen |
| Xbox hardware | Primary target; Series S used for testing |

### What changed recently

- **Performance.** Pipelined frame readback, precise frame pacing (a fixed schedule on a high-resolution timer), shaders compiled in the background so the game no longer freezes while the console compiles them, and a larger audio buffer against crackling. A per-minute performance line goes in the log.
- **Stability.** A guard around the console's shader compiler so one bad shader skips its draws rather than killing the app; Vulkan vertex-format fixes (Persona 4 Golden); an OpenGL render-target fix. Leaving a game and starting another restarts the app cleanly.
- **Frontend.** A PS3-style XMB (Settings → Appearance → Home screen), game pages with a hero or screenshot backdrop, a SteamGridDB art picker, and a diagnostics report.

### Known issues

- **Minecraft** runs on Vulkan only; it crashes on OpenGL. Some text is garbled and it runs slowly.
- **Danganronpa** has an interaction bug that is also open in upstream Vita3K, with no known cause yet.
- Some games' system save dialog can get stuck. Holding Select/View for about three seconds writes diagnostics to the log.
- Some games work on only one renderer. Switch renderer in the game's settings if one misbehaves.

## Try a development build

1. Open **[Actions → Full core APPX](https://github.com/oledz-i/Vita3K-UWP/actions/workflows/full-core-appx.yml)** and pick the latest successful run on `astra-undertale`.
2. Download the artifact named **`Vita3K-Build-Output-x64-<run number>`**. It holds the signed APPX, the test certificate and install instructions.
3. Follow the [install guide](docs/INSTALL.md) to deploy through Xbox Dev Mode.

Artifacts expire after 14 days, and GitHub may ask you to sign in. Release builds hide the developer Tools page; see [releasing](docs/RELEASING.md).

If something goes wrong, use **Settings → Diagnostics** or the Xbox Device Portal (File Explorer → LocalAppData → the app's `LocalState` folder) holds `vita3k-runtime.log`. Attach it to an issue, together with the game and the renderer.

## What you need to bring

This project ships no games, firmware or keys.

- **Firmware.** The system firmware (Sony's `PSP2UPDAT.PUP`, also seen as `PSVUPDAT.PUP`, same file) is required; the preinstalled package and fonts are separate Sony downloads, recommended. Pick them all at once under any names: the app tells them apart by content and installs them immediately. See the [install guide](docs/INSTALL.md#5-firmware).
- **Games.** `.vpk`, `.zip`, `.pkg` with its license, or an extracted game folder, on internal storage or a USB drive.

## Project notes

- [Roadmap](docs/ROADMAP.md) and [core port notes](docs/UWP-CORE-PORT-NOTES.md)
- [Graphics plan](docs/RENDERERS.md) and [OnyxEMU GPU integration](docs/ONYX_GPU_PORT.md)
- [Upstream research](docs/UPSTREAM.md)

## Building and helping

The shell uses Visual Studio 2022, C++ UWP tools, and Windows SDK 10.0.22621.0. The hosted workflow uses `windows-2022`; you do not need to register a self-hosted runner or provide signing secrets. See [BUILDING.md](docs/BUILDING.md) for the local command and [CONTRIBUTING.md](CONTRIBUTING.md) for scope and issue reporting.

Useful first contributions include per-game compatibility reports with logs, performance captures, and D3D12/Dozen driver investigations. Please report port issues **in this repository**, not to upstream Vita3K.

## Credits and licensing

The emulation foundation is **[Vita3K](https://github.com/Vita3K/Vita3K)**. Full credit belongs to its developers and dependency authors for their existing emulator. Our reviewed upstream revision and source map are in [UPSTREAM.md](docs/UPSTREAM.md).

New original code here is GPL-2.0-or-later; upstream and dependencies retain their own licenses and notices. See [COPYING.txt](COPYING.txt) and [NOTICE.md](NOTICE.md). No games, firmware, keys, or proprietary SDK files are included.

This is an independent community project, not an official Vita3K release and not affiliated with or endorsed by Sony or Microsoft. PlayStation and Xbox names belong to their respective owners.
