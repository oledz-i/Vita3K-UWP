# Install and test on Xbox

**Pre-alpha.** The package contains the full Vita3K core, but no game has been confirmed to boot on Xbox yet. Expect crashes, and send the log (below) when one happens.

## 1. Get the build

On GitHub, open `oledz-i/Vita3K-UWP` → **Actions** → **Full Vita3K core + Dozen signed APPX (gameplay path)**. Pick the latest successful run on `astra-undertale` and download **`Vita3K-Build-Output-x64-<run number>`**. That is the one to install. The other artifacts are build intermediates and logs.

Unzip it. Inside are the `.appx`, any x64 dependencies under `Dependencies/`, `Development.cer`, `build-info.json` and `SHA256SUMS.txt`.

## 2. Deploy through Developer Mode

1. Put the Xbox in Developer Mode ([Microsoft's guide](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/devkit-activation)).
2. Open the Xbox Device Portal from a PC on the same network: `https://<xbox-ip>:11443`.
3. **Add** the `.appx`, plus the x64 dependencies if the portal asks. Skip ARM and x86 ones.
4. Builds from different CI runs carry different test certificates, so a newer build may need the old one uninstalled first. **Uninstalling deletes the app's data**, including whatever Vita3K data you kept in internal storage. Data kept on a USB drive stays.

## 3. Set Vita3K to run as a Game

In **Dev Home**, highlight Vita3K, press **Menu**, choose **View details**, and set **App type** to **Game**. As an *App* the Xbox gives it a small amount of memory and closes it without warning; as a *Game* it gets several GB.

## 4. Put your files on a USB drive

Any USB drive the Xbox can read (exFAT or NTFS). For example:

```
E:\Vita\Games\PCSE01116\      a game folder (eboot.bin and sce_sys inside)
E:\Vita\Games\Minecraft.vpk   a .vpk
E:\Vita\Firmware\             the .PUP files
E:\Vita3K\                    Vita3K's data, if you choose to keep it on the drive
```

Games are read from the drive where they are; nothing is copied to internal storage.

## 5. Firmware

Vita3K needs Sony's PS Vita firmware. This project ships none of it. Download it on a PC:

| Part | Needed | Where |
| --- | --- | --- |
| System firmware | Required | Sony's PS Vita system software page. It downloads as `PSP2UPDAT.PUP`; older copies are named `PSVUPDAT.PUP`. **They are the same file**, the system update. |
| Preinstalled package | Recommended | A separate, smaller Sony download, linked by the Vita3K libretro core: <https://bit.ly/4hlePsX> |
| Fonts | Recommended (some text is missing without it) | A separate Sony download: <https://bit.ly/48ouDaa> |

The preinstalled package and fonts download under the same `PSP2UPDAT.PUP` name too, so keep them in separate folders or rename them. The names don't matter to the app.

In the app: **Settings → Firmware → Install firmware files**, and select all the `.PUP` files at once (or **Install from a folder**). The app looks inside each package, works out which part it is, and installs it **immediately** with a progress bar. The three tiles then show what is installed and its version. If you pick the same update twice, the summary says so.

## 6. Where Vita3K keeps its data

**Settings → Storage → Vita3K data** holds the firmware, installed games, saves and caches. Vita3K *installs* a game into this folder the first time it runs it (and decrypts it there). With little internal space, choose **Use a folder on a drive**: the app creates a `Vita3K` folder there and offers to move what is already installed.

## 7. Play

**Settings → Storage → Choose games folder**: a folder of games, or one game's own folder. Folders need `eboot.bin` and `sce_sys` directly inside. Then pick the game in the Library and start it. The first start of a game takes longer while Vita3K installs and decrypts it.

## 8. Emulator settings

**Settings → Emulation** lists Vita3K's options for graphics, CPU, content, system, audio and network. They are saved on the console and take effect when a game starts.

## 9. When something goes wrong: the log

The core writes `vita3k-runtime.log` as it runs. In the Device Portal, open **File Explorer → LocalAppData → oledz.Vita3KUWP.Dev_… → LocalState** and download `vita3k-runtime.log`. Send that with the commit from `build-info.json`, the console, the game and its format, and what was on screen when it stopped.

Do not report this port's problems to upstream Vita3K.
