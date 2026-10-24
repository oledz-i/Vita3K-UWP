# Streaming-style Xbox frontend

The frontend on the `core-integration` branch is a **real native UWP UI**, rather than a static mockup. It is designed for controller focus and large-screen viewing, but Xbox controller navigation, image decoding, and media playback still need console testing.

## Navigation

| Page | What it does |
| --- | --- |
| Discover | Cinematic welcome banner, library shelves, quick actions |
| Library | Search local entries, add individual game files, scan an accessible folder, verify file headers and find missing cover art |
| Game details | Preview detected format and validation result, select an owned PKG license, manage cover art and inspect boot blockers |
| Settings | Change theme, music, UI click/focus sounds, sound effect volume, storage, firmware selection and art key |
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

## Navigation audio and Xbox B button

Four additional **original synthesized effects** complement the four music loops:

- **Focus:** gentle rising glass ping when a game card or setting gains focus
- **Select:** brief two-tone confirmation on button presses and game cards
- **Back:** falling soft cue when Xbox B / Escape / BrowserBack returns to a previous page
- **Shelf swipe:** airy descending cue at the end of a horizontal game-shelf scroll

Choose **Settings → Interface sounds** to turn cues off or on, adjust their separate volume, or preview a selection cue. The sounds are independent of the home soundtrack and persist across launches. Xbox behavior needs hardware validation.

## Game discovery and metadata

Choose a folder using **Settings → Storage and paths** or the Discover shortcut. The app remembers the granted folder access (Xbox UWP picker limitations and attached drive visibility vary). A scan reads up to 300 top-level entries: **`.vpk`, `.zip`, `.pkg`, `.vci`, `eboot.bin`, and subfolders**. Alternatively, **Add Vita file** from Discover or Library opens a file picker and remembers up to 64 individual files. Selected files are stored using Xbox UWP access tokens. Sources on disconnected USB/external drives will not load until available again. An extracted title's `sce_sys/param.sfo` is read when present to recover the title, and its `sce_sys/icon0.png` can be shown as a local icon fallback. Folder entries are *candidates*: the app checks for `eboot.bin` and validates the first 64 bytes of that executable. The app checks ZIP/VPK and Sony PKG signatures, identifies ARM ELF or Sony SELF headers, and marks VCI images for further inspection by the missing upstream VCI parser. It does **not** decode, decompress, decrypt, install, or execute any game. A recognized file extension is not proof of valid game content.

**PKG:** The Game Details page can save permission to an owner-supplied `.rif`, `.zrif` or `work.bin` license file, but has no decryption or license-validation engine. **Commercial games:** The original Vita3K emulator can run some legitimately dumped commercial titles, but this UWP port cannot launch any titles yet. Older Vitamin VPK dumps are not supported by upstream Vita3K.

A future version must install package contents into a Vita filesystem, parse full VCI/archive structures, handle nested directories, map SELF and ELF modules, integrate Dynarmic CPU/kernel and GPU subsystems, and then add reliable per-title compatibility checks.

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

- Confirm Discover, Library, Settings and Developer pages can be navigated using an Xbox controller. Press **B** to go back.
- Select all five themes; restart to confirm persistence.
- Switch all four music loops, then test focus/click/back/shelf sounds, cue volume, and the UI sounds toggle.
- Select storage on the internal drive or attached storage that the picker exposes.
- Scan extracted `sce_sys/param.sfo` / `eboot.bin` game directories and test `.vpk`, `.zip`, `.pkg` and `.vci` candidates. Try adding an individual file.
- Select your own cover, and optionally try SteamGridDB with your key.
- Run the memory component test and D3D12 clear/framebuffer preview.
- Send the source commit, package version, Xbox model and exact failures; avoid exposing secrets or private file paths.
