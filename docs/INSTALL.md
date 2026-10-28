# Install on Xbox

**Alpha.** Several games run on Xbox (see [compatibility](COMPATIBILITY.md)), but expect crashes and rough performance. Send the log (below) when one happens.

## 1. Get the package

Download the latest release from [Releases](https://github.com/oledz-i/Vita3K-UWP/releases) and unzip it. Inside are the `.appx`, its x64 dependencies under `Dependencies/`, and the certificate it's signed with.

## 2. Deploy through Developer Mode

1. Put the Xbox in Developer Mode ([Microsoft's guide](https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/devkit-activation)).
2. Open the Xbox Device Portal from a PC on the same network: `https://<xbox-ip>:11443`.
3. **Add** the `.appx`, plus the x64 dependencies if the portal asks. Skip ARM and x86 ones.
4. A newer release may need the old one uninstalled first. **Uninstalling deletes the app's data**, including any Vita3K data kept on internal storage. Data kept on a USB drive stays, which is one more reason to keep it there.

## 3. Set Vita3K to run as a Game

In **Dev Home**, highlight Vita3K, press **Menu**, choose **View details**, and set **App type** to **Game**. As an *App* the Xbox gives it a small amount of memory and closes it without warning; as a *Game* it gets several GB.

## 4. Get started on Home

Home shows a **Get started** card with three steps until they are done:

1. **Choose where Vita3K keeps its data.** The USB drives the console sees are listed; pick one and Vita3K uses a `Vita3K` folder on it. Internal storage is there too, but installed games are large, so a drive is the better place.
2. **Install the firmware** (below).
3. **Choose your games folder.**

## 5. Put your files on a USB drive

Any USB drive the Xbox can read (exFAT or NTFS). For example:

```
E:\Vita\Games\PCSE01116\      a game folder (eboot.bin and sce_sys inside)
E:\Vita\Games\Minecraft.vpk   a .vpk
E:\Vita\Firmware\             the .PUP files
E:\Vita3K\                    Vita3K's data, if you choose to keep it on the drive
```

Games are read from the drive where they are; nothing is copied to internal storage.

## 6. Firmware

Vita3K needs Sony's PS Vita firmware. This project ships none of it. Download it on a PC:

| Part | Needed | Where |
| --- | --- | --- |
| System firmware | Required | Sony's PS Vita system software page. It downloads as `PSP2UPDAT.PUP`; older copies are named `PSVUPDAT.PUP`. **They are the same file**, the system update. |
| Preinstalled package | Recommended | A separate, smaller Sony download, linked by the Vita3K libretro core: <https://bit.ly/4hlePsX> |
| Fonts | Recommended (some text is missing without it) | A separate Sony download: <https://bit.ly/48ouDaa> |

The preinstalled package and fonts download under the same `PSP2UPDAT.PUP` name too, so keep them in separate folders or rename them. The names don't matter to the app.

In the app: **Settings → Firmware → Install firmware files**, and select all the `.PUP` files at once (or **Install from a folder**). The app looks inside each package, works out which part it is, and installs it **immediately** with a progress bar. The three tiles then show what is installed and its version. If you pick the same update twice, the summary says so.

## 7. Where Vita3K keeps its data

**Settings → Storage → Vita3K data** holds the firmware, installed games, saves and caches. Vita3K *installs* a game into this folder the first time it runs it (and decrypts it there). With little internal space, choose **Use a folder on a drive**: the app creates a `Vita3K` folder there and offers to move what is already installed.

## 8. Play

**Settings → Storage → Choose games folder**: a folder of games, or one game's own folder. Folders need `eboot.bin` and `sce_sys` directly inside. Then pick the game in the Library and press **Play**. The game takes the whole screen; a loading screen shows what Vita3K is doing (installing and decrypting with a percentage the first time, then preparing graphics and starting) until the game shows its first picture. Press **View + Menu** together to quit back to the library. Starting a second game after quitting restarts the app first, then boots it.

## 9. Emulator settings

**Settings → Emulation** starts with the renderer (Vulkan or OpenGL, both running on Direct3D 12; if a game misbehaves, try the other) and the screen mode (Fit or Stretch), then Vita3K's own options: graphics (internal resolution, filtering, shader compiling), the **JIT recompiler (Dynarmic)**, content, system, audio and network. They are saved on the console and take effect when a game starts.

## 10. When something goes wrong: the log

The core writes `vita3k-runtime.log` as it runs. In the Device Portal, open **File Explorer → LocalAppData → Vita3KUWP_… → LocalState** and download `vita3k-runtime.log`. Send it along with the game and its format, the renderer, and what was on screen when it stopped.

Do not report this port's problems to upstream Vita3K.
