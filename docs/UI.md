# Streaming-style Xbox frontend

The frontend on the `core-integration` branch is a **real native UWP UI**, rather than a static mockup. It is designed for controller focus and large-screen viewing, but Xbox controller navigation, image decoding, and media playback still need console testing.

## Navigation

| Page | What it does |
| --- | --- |
| Discover | Cinematic welcome banner, library shelves, quick actions |
| Library | Search local entries, rescan a selected game folder, find missing cover art |
| Game details | Preview a candidate title, replace its cover art, request an exact SteamGridDB match |
| Settings | Choose theme, menu soundtrack, volume, folder, firmware selection, artwork API key |
| Developer | Run real Vita3K bitmap allocator tests and D3D12 clear / BGRA upload |

No game shown in the library can be booted by this build. The app does not contain the Dynarmic CPU, Vita kernel and firmware installer, GXM renderer, or shader translator needed to launch software.

## Themes

Select **Settings → Appearance** to change between:

- **Onyx:** dark graphite and neon violet
- **Aurora:** deep ocean and soft teal
- **Ember:** dark burgundy and amber
- **Glacier:** bright ice-blue, white cards and deep blue
- **Orbit:** dark indigo and celestial lavender

Colors are saved locally and applied immediately. The UI uses the Windows **Segoe UI** typeface and wide, scrollable game shelves.

## Music

**Settings → Home menu soundtrack** offers four original, programmatically generated ambient loops: **Ambient Drift**, **Neon Pulse**, **Stardust**, and **Tidal Blue**. They contain no copyrighted console themes or external soundtrack files. You can also select **Off** or supply your own supported MP3/WAV/M4A file using the Xbox picker. Set a separate menu music volume. The custom file uses UWP's FutureAccessList to retain permission where supported.

## Game discovery and metadata

Choose a folder using **Settings → Storage and paths** or the Discover shortcut. The app remembers the granted folder access (Xbox UWP picker limitations and attached drive visibility vary). A scan reads up to 300 top-level entries: `.vpk`, `.pkg`, `.zip`, and subfolders. An extracted title's `sce_sys/param.sfo` is read when present to recover the title, and its `sce_sys/icon0.png` can be shown as a local icon fallback. Folder entries are *candidates*; the scan cannot validate a complete game or launch anything.

A future version should handle deeper trees, Vita title IDs within package archives, corrupted metadata, installed content discovery, and robust migration of library paths.

## Artwork

From a game card, choose **Choose custom box art** and select your image. The app copies the file into its own local `Artwork` cache. Manual artwork has priority over remote artwork.

Optional **SteamGridDB** lookup:

1. Generate your own API key through SteamGridDB's developer preferences.
2. Open **Settings → Game artwork** and save the key, which is stored through Windows PasswordVault rather than in the repository.
3. Select **Get missing covers**, or search from a game card.
4. The client searches the title and only stores a 600 × 900 grid if the API returns an exact name match.

Not every Vita title exists on SteamGridDB, and an exact title can occasionally correspond to artwork for another platform. **Correct cover art for every game is not guaranteed.** For missing, ambiguous, or incorrectly matched artwork, choose your own image. Remote image URLs are remembered, but may need an internet connection when viewed again. Nothing requires SteamGridDB to use the offline library.

## Firmware

The **Choose official firmware .PUP** button saves file access; it **does not install the firmware**. Obtain official firmware through Sony's supported system software distribution and place it on storage accessible to the console picker. Firmware installation will require the upstream core.

## Vita3K settings parity

Existing functional settings: themes, menu soundtrack/volume, storage selection, firmware selection, cover art sources. The following upstream setting groups are represented only as an explicit **pending integration** explanation, not fake switches:

- CPU optimization, Dynarmic, LLE module settings and file-loading behavior
- OpenGL / Vulkan vs Xbox D3D12, resolution multiplier, filters, anisotropic filtering, shader cache, surface sync, texture imports and GPU validation
- Audio backend, emulated NGS audio, audio volume
- PSTV mode, system language, date/time, confirm button, touch and physical controls
- Advanced memory mapping, logging, debugging, tracing and per-game overrides

The full upstream settings list will only become effective once those subsystems are linked. The memory allocator and D3D12 *presentation* code in this branch do not constitute a playable emulator.

## Test checklist

- Confirm Discover, Library, Settings and Developer pages can be navigated using an Xbox controller.
- Select all five themes; restart to confirm persistence.
- Switch all four music loops; change volume and disable sound.
- Select storage on the internal drive or attached storage that the picker exposes.
- Scan a directory with extracted Vita title metadata and `.vpk` packages.
- Select your own cover, and optionally try SteamGridDB with your key.
- Run the memory component test and D3D12 clear/framebuffer preview.
- Send the source commit, package version, Xbox model and exact failures; avoid exposing secrets or private file paths.
