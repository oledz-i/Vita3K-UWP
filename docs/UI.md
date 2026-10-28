# Frontend

A native UWP interface made for a controller on a TV. Everything is reachable with the Xbox controller; **B** goes back.

## Home layouts

**Settings → Appearance → Home screen** switches between:

- **Cinematic** (default): a hero for the game you last played, shelves, and a left rail with Home, Library and Settings.
- **PS3 XMB**: a cross-bar in the style of the PS3. Categories run along the top (Settings, Music, Game, Network), items run down beneath the selected one, and animated waves sit over a background colour that follows the month (darker at night). A clock shows the date and time. D-pad or stick to move, **A** to select, **B** to back out.

## Pages

| Page | What it does |
| --- | --- |
| Home | Continue playing, collection shelves, quick actions, a Get started card until storage, firmware and games are set |
| Library | Search, add individual files or scan a folder, validate files, find missing covers |
| Game page | Backdrop, cover, Play, artwork, per-game settings, detected format and title ID |
| Settings | Firmware, storage, emulation, appearance, music and sounds, artwork, diagnostics |
| Tools | Development builds only: CPU, memory, JIT, Vulkan and D3D12 diagnostics |

## Artwork and backdrops

- **Covers:** pick your own image, or fetch from SteamGridDB.
- **Art picker:** from a game page, search SteamGridDB, browse covers and backdrops (hero images), and pick the ones you like. **B** closes it.
- **Backdrops:** a SteamGridDB hero if you chose one, otherwise a screenshot the app saves from the game once it is showing a proper picture. The backdrop appears behind the game page and the Home hero.
- The SteamGridDB API key is saved in the Windows PasswordVault, not in the repository. Only images served from `steamgriddb.com` are accepted. Not every Vita game exists there, and matches can be wrong, so your own image always wins.

## Themes, music and sounds

Five themes (Onyx, Aurora, Ember, Glacier, Orbit). Four original home soundtracks (Afterglow, Neon Arcade, Stardust, Tidal), composed for this project by `scripts/music/compose.py`, or your own file, or off. Original interface cues for focus, select, back and shelf swipes, each with its own volume.

## In a game

The game takes the full screen, with a loading screen until its first frame. **View + Menu** together quits to the library. A **touch cursor** driven by a stick stands in for the touch screen. Quitting a game and starting another restarts the app first, which keeps the next game clean.

## Settings

- **Emulation:** 24 Vita3K options (renderer, screen mode, resolution, filtering, shader compiling, Dynarmic, content, system, audio, network). All are read by the core; most apply when a game starts, and volume and a few others apply live.
- **Storage:** where Vita3K keeps its data (internal or a folder on a drive), and the games folder.
- **Firmware:** install the `.PUP` files; the app identifies each by content.
- **Diagnostics:** writes a report to the log. The log also records a performance line every minute (frames, slow frames, audio underruns).

## Checklist for testers

- Navigate every page with the controller, switch both home layouts, and try all five themes.
- Play a game on each renderer, then quit and start a second game.
- Try the art picker with and without a SteamGridDB key.
- Send the build number, console, game, renderer and `vita3k-runtime.log` with any report.
