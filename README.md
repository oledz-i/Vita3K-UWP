<p align="center"><img src="docs/assets/banner.svg" alt="Vita3K UWP — A new home for Vita emulation" width="100%"></p>

<p align="center">
  <a href="https://github.com/oledz-i/Vita3K-UWP/actions/workflows/uwp-package.yml"><img src="https://github.com/oledz-i/Vita3K-UWP/actions/workflows/uwp-package.yml/badge.svg" alt="UWP package build"></a>
  <img src="https://img.shields.io/badge/stage-pre--alpha-38bdf8" alt="Pre-alpha">
  <img src="https://img.shields.io/badge/target-Xbox%20Dev%20Mode-107c10" alt="Xbox Dev Mode target">
  <a href="COPYING.txt"><img src="https://img.shields.io/badge/license-GPL--2.0--or--later-blue" alt="GPL license"></a>
</p>

<p align="center"><b>An independent, experimental Vita3K port for Xbox Series X|S, built on UWP.</b><br>Native console UI. Open development. Built on the work of the Vita3K community.</p>

<p align="center"><a href="docs/INSTALL.md">Install</a> · <a href="docs/BUILDING.md">Build</a> · <a href="docs/ROADMAP.md">Roadmap</a> · <a href="docs/RENDERERS.md">Renderers</a></p>

<p align="center"><img src="docs/assets/screens/home.png" alt="Home screen of the Xbox frontend" width="100%"></p>

<table>
<tr>
<td width="50%"><img src="docs/assets/screens/library.png" alt="Library"></td>
<td width="50%"><img src="docs/assets/screens/settings.png" alt="Settings"></td>
</tr>
</table>
<p align="center"><sub>Rendered previews of the frontend at the Xbox's 960×540 layout size. Library games and cover art are sample placeholders.</sub></p>

## Where things stand

**Pre-alpha. Nothing is playable yet.** The pinned Vita3K libretro core now compiles for UWP and ships inside the APPX together with the Mesa Dozen Vulkan-on-Direct3D12 layer. The first on-console test launched the core and reached firmware installation, then crashed. Fixing that is the current work. No game has booted in this port, and there are no compatibility or performance claims.

| Area | Status |
| --- | --- |
| UWP app and packaging | Signed x64 APPX built by GitHub Actions |
| Vita3K core | Compiles and packages for UWP (WizzardSK/vita3k-libretro, pinned) |
| Graphics | Vulkan through Mesa Dozen to D3D12, from the OnyxEMU work |
| CPU | Dynarmic with a UWP W^X patch |
| Firmware install | Crashes on first console test; under investigation |
| Game loading | Single files only; whole-folder games (for example extracted dumps) are next |
| Frontend | Home, Library, Settings, five themes, original music and interface sounds, artwork |
| Settings | Appearance and storage only; emulator options are being added |
| Xbox hardware | Primary target; Series S used for testing |

## Try a development build

1. Gameplay work happens on the **[`astra-undertale` branch](https://github.com/oledz-i/Vita3K-UWP/tree/astra-undertale)**. Open its **Full core APPX** workflow under Actions and pick the latest successful run.
2. Download the artifact named **`Vita3K-Build-Output-x64-<run number>`**. It holds the signed APPX, the test certificate and install instructions.
3. Follow the [install guide](docs/INSTALL.md) to deploy through Xbox Dev Mode.

Artifacts expire after 14 days, and GitHub may ask you to sign in. Release builds hide the developer Tools page.

If the app crashes, the Xbox Device Portal (File Explorer → LocalAppData → the app's `LocalState` folder) holds `vita3k-runtime.log`. Attach it to an issue.

## What you need to bring

This project ships no games, firmware or keys.

- **Firmware (two files, one optional).** Sony distributes all three under the same name, `PSP2UPDAT.PUP`, so rename the preinstalled package and font package before picking them.
  - Main system firmware, required.
  - Preinstalled package (name contains `PREINST`), required.
  - Font package (name contains `FONT`), optional.
- **Games.** `.vpk`, `.zip`, `.pkg` with its license, or an extracted game folder.

## Project notes

- [Roadmap](docs/ROADMAP.md)
- [Graphics plan](docs/RENDERERS.md)
- [Upstream research](docs/UPSTREAM.md)

## Building and helping

The shell uses Visual Studio 2022, C++ UWP tools, and Windows SDK 10.0.22621.0. The hosted workflow uses `windows-2022`; you do not need to register a self-hosted runner or provide signing secrets. See [BUILDING.md](docs/BUILDING.md) for the local command and the issue tracker for scope and issue reporting.

Useful first contributions include reproducible Xbox launch reports, UWP dependency audits, JIT/memory feasibility tests, and D3D12 capability investigations. Please report port issues **in this repository**, not to upstream Vita3K.

## Credits and licensing

The planned emulation foundation is **[Vita3K](https://github.com/Vita3K/Vita3K)**. Full credit belongs to its developers and dependency authors for their existing emulator. Our reviewed upstream revision and source map are in [UPSTREAM.md](docs/UPSTREAM.md).

New original code here is GPL-2.0-or-later; upstream and dependencies retain their own licenses and notices. See [COPYING.txt](COPYING.txt) and [NOTICE.md](NOTICE.md). No games, firmware, keys, or proprietary SDK files are included.

This is an independent community project, not an official Vita3K release and not affiliated with or endorsed by Sony or Microsoft. PlayStation and Xbox names belong to their respective owners.
