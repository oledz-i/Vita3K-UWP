# Install and test on Xbox

**Pre-alpha.** The package contains the full Vita3K core, but no game has been confirmed to boot on Xbox yet. Expect crashes, and send the log (below) when one happens.

## 1. Get the build

On GitHub, open `oledz-i/Vita3K-UWP` → **Actions** → **Full Vita3K core + Dozen signed APPX (gameplay path)**. Pick the latest successful run on `astra-undertale` and download **`Vita3K-Build-Output-x64-<run number>`**. That is the one to install. The other artifacts are build intermediates and logs.

Unzip it. Inside are the `.appx`, any x64 dependencies under `Dependencies/`, `Development.cer`, `build-info.json` and `SHA256SUMS.txt`.

## 2. Deploy through Developer Mode

1. Put the Xbox in Developer Mode ([Microsoft's guide](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/devkit-activation)).
2. Open the Xbox Device Portal from a PC on the same network: `https://<xbox-ip>:11443`.
3. **Add** the `.appx`, plus the x64 dependencies if the portal asks. Skip ARM and x86 ones.
4. Launch it from Dev Home. Each CI run signs with a new test certificate, so a newer build may need the old one uninstalled first. **Uninstalling deletes the app's data, firmware and copied games included.**

## 3. Firmware

Vita3K needs Sony's PS Vita firmware. This project ships none of it. Download on a PC:

| Part | Status | Where | Download name | Rename to (suggested) |
| --- | --- | --- | --- | --- |
| System firmware | Required | Sony's PS Vita system software page | `PSP2UPDAT.PUP` (older copies: `PSVUPDAT.PUP`) | keep the name |
| Preinstalled package | Required | Sony's update servers; Vita3K's Discord gives the link (`-firmware` command) | `PSP2UPDAT.PUP` | `PREINST.PUP` |
| Font package | Optional, but some games show no text without it | Sony's update servers; same as above | `PSP2UPDAT.PUP` | `FONT.PUP` |

All three downloads share one name, so rename the second and third before copying them to a USB drive or network share. In the app, open **Settings → Firmware** and choose each file in its own slot. The app checks that each one is a PlayStation update package and refuses a file whose name says it is another part.

The firmware installs the first time a game starts, which takes a few minutes. Each slot then shows **Installed**. If an install went wrong, **Reinstall firmware on next start** removes the installed parts but keeps the chosen files.

## 4. Games

Supported: `.vpk`, `.zip`, `.pkg` with its license (`work.bin`, `.rif` or `.zrif`), or an extracted game folder (the folder that holds `eboot.bin` and `sce_sys`).

- **Choose a folder** can be a folder of games or a single game's folder.
- On first start the app copies the game into its own storage, because Vita3K can only read files there. A large game takes a while. Later starts reuse the copy.
- An `eboot.bin` picked on its own cannot start. Add its whole folder instead.

## 5. Emulator settings

**Settings → Emulation** lists Vita3K's options for graphics, CPU, content, system, audio and network. They are saved on the console and take effect when a game starts. **Restore default emulation options** puts them back.

## 6. When something goes wrong: the log

The core writes `vita3k-runtime.log` as it runs. In the Device Portal, open **File Explorer → LocalAppData → oledz.Vita3KUWP.Dev_… → LocalState** and download `vita3k-runtime.log`. Send that with:

- the commit from `build-info.json`
- the console (Series S or X)
- the game and its format
- what was on screen when it stopped

Do not report this port's problems to upstream Vita3K.
