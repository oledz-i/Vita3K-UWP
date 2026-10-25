# Install the development UI

**This package cannot run PS Vita games.** On the `core-integration` branch it contains a native streaming-style catalog UI, Vita3K allocator diagnostics and a D3D12 display bootstrap. Xbox behavior still needs testing.

## Get the files

On GitHub, open `oledz-i/Vita3K-UWP` → Actions → UWP development package. Choose a successful run and choose the **core-integration** branch build and download `Vita3K-UWP-core-d3d12-x64-<run number>`. The default `main` branch currently has the older development shell. Extract the ZIP. Locate the application `.appx` and any x64 dependencies below `Dependencies/`. The artifact also includes `Development.cer`, `build-info.json`, and `SHA256SUMS.txt`.

## Optional Onyx-backed GPU prototype

For the `onyx-dozen-integration` branch, use the separate [Onyx Dozen build workflow](https://github.com/oledz-i/Vita3K-UWP/actions/workflows/onyx-dozen.yml). If **both** the Mesa UWP build and signed APPX package jobs succeed, download the artifact whose name begins `Vita3K-UWP-ONYX-Dozen-x64-`. It includes the genuine patched `vulkan_dzn.dll` GPU driver and `dxil.dll` shader support dependency inside the APPX. The `source-host-smoke` artifact compiles the same host without Dozen, for safe comparison.

On your Xbox, open **Developer → Probe actual packaged Vulkan device**. Record whether the test reports driver discovery, a Vulkan instance, GPU adapters and a created logical graphics device. If there is a `VkResult` or loader error, capture the exact text. This is **not** a PS Vita game boot test: the CPU/core and Vita Vulkan frame bridge are still required.

The prototype code derives from your [OnyxEMU renderer](https://github.com/oledz-i/OnyxEMU) and carries GPLv3 attribution in `COPYING.GPLv3.txt` and `NOTICE.md`. See [renderer source details](ONYX_GPU_PORT.md).

## Xbox Series X|S Developer Mode

1. Activate and enter Xbox Developer Mode using Microsoft's official setup instructions.
2. Open the console's Xbox Device Portal from a computer on the same network.
3. Use its app deployment page to upload the application APPX. Add the included x64 runtime dependencies if requested. Do not upload ARM or x86 dependencies.
4. Deploy and launch **Vita3K UWP Development** from Dev Home.
5. Expected result: the new **Discover** page with a featured banner and library shelves. Navigate to **Settings** to try five themes, four original menu tracks and four interface cues (focus, select, back and swipe). Cues have an independent on/off setting and volume slider.
6. Navigate the interface with Xbox controls, including **B** to return to the preceding page. Try scanning a folder containing extracted Vita games and supported file formats, or add an individual VPK/ZIP/PKG/VCI file. Open a card to see its input-header check. The test will not launch a game.
7. Navigate to **Developer** and select **Check for actual Vita3K core** and **Probe actual packaged Vulkan device**. The normal APPX does not contain a full UWP Vita3K DLL or Mesa Dozen ICD, so both may report missing. These are **missing-dependency diagnostics**, not reasons to assume Xbox is incompatible. An experimental UWP source compilation report is recorded in the GitHub Actions port-probe artifact.
8. Run the Vita3K allocator test and initialize the D3D12 preview. The test should paint a native frame on Xbox; a successful Windows APPX build does not prove the console API will work.
9. Record whether the interface launches, controller focus works, menu music plays, stored paths and custom artwork persist, and the app can resume and close normally. **Do not expect game booting, even for homebrew:** the executable loader, CPU, kernel and Vita GXM renderer remain unavailable in the Xbox port.

Developer Mode and Device Portal setup: https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/devkit-activation and https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/device-portal-xbox . Portal controls can differ across console versions. If certificate validation fails, record the exact portal error rather than disabling security checks or treating a successful build as a deployment guarantee.

This frontend makes no claim to expanded game resources. Later renderer and memory tests must log the actual resource limits on hardware. No retail-mode installation is provided.

## Windows test installation

Use a Windows system configured for development/sideloading. Inspect the certificate and package provenance before trusting the included **public development certificate**. If needed, import it into **Local Computer → Trusted People** using Windows Certificate Import Wizard, then install any included x64 dependencies and the APPX. Remove the test certificate when finished. Never import it into Trusted Root Certification Authorities.

Each CI run creates a new test certificate. In-place upgrades across builds may require removing the previous shell and trusting the new certificate. Uninstalling an app removes its local app data; this development UI has no emulator saves, but future builds will require backup instructions and a stable release-signing identity.

APPX is the supplied format. Changing its extension to `.msix` does not port or improve the app.

## Report results

Include source commit and package version from `build-info.json`, console model/OS, deployment error code if any, and what appears after launch. Attach relevant logs after removing private paths or account details. Use this repository's bug report template. Read [frontend usage](UI.md) for themes, artwork, soundtracks and metadata behavior, and [real core port status](CORE_PORT.md) for the upstream libretro and Dozen experiments. Do not file this port's issues against upstream Vita3K.
