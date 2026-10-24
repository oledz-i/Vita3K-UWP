# Install the development UI

**This package cannot run PS Vita games.** On the `core-integration` branch it contains a native streaming-style catalog UI, Vita3K allocator diagnostics and a D3D12 display bootstrap. Xbox behavior still needs testing.

## Get the files

On GitHub, open `oledz-i/Vita3K-UWP` → Actions → UWP development package. Choose a successful run and choose the **core-integration** branch build and download `Vita3K-UWP-core-d3d12-x64-<run number>`. The default `main` branch currently has the older development shell. Extract the ZIP. Locate the application `.appx` and any x64 dependencies below `Dependencies/`. The artifact also includes `Development.cer`, `build-info.json`, and `SHA256SUMS.txt`.

## Xbox Series X|S Developer Mode

1. Activate and enter Xbox Developer Mode using Microsoft's official setup instructions.
2. Open the console's Xbox Device Portal from a computer on the same network.
3. Use its app deployment page to upload the application APPX. Add the included x64 runtime dependencies if requested. Do not upload ARM or x86 dependencies.
4. Deploy and launch **Vita3K UWP Development** from Dev Home.
5. Expected result: the new **Discover** page with a featured banner and library shelves. Navigate to **Settings** to try five themes, four original menu tracks and four interface cues (focus, select, back and swipe). Cues have an independent on/off setting and volume slider.
6. Navigate the interface with Xbox controls, including **B** to return to the preceding page. Try scanning a folder containing extracted Vita games and supported file formats, or add an individual VPK/ZIP/PKG/VCI file. Open a card to see its input-header check. The test will not launch a game.
7. Navigate to **Developer** to run the upstream allocator test and initialize the D3D12 preview. The test should paint a native frame on Xbox; a successful Windows APPX build does not prove the console API will work.
8. Record whether the interface launches, controller focus works, menu music plays, stored paths and custom artwork persist, and the app can resume and close normally. **Do not expect game booting, even for homebrew:** the executable loader, CPU, kernel and Vita GXM renderer remain unavailable in the Xbox port.

Developer Mode and Device Portal setup: https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/devkit-activation and https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/device-portal-xbox . Portal controls can differ across console versions. If certificate validation fails, record the exact portal error rather than disabling security checks or treating a successful build as a deployment guarantee.

This frontend makes no claim to expanded game resources. Later renderer and memory tests must log the actual resource limits on hardware. No retail-mode installation is provided.

## Windows test installation

Use a Windows system configured for development/sideloading. Inspect the certificate and package provenance before trusting the included **public development certificate**. If needed, import it into **Local Computer → Trusted People** using Windows Certificate Import Wizard, then install any included x64 dependencies and the APPX. Remove the test certificate when finished. Never import it into Trusted Root Certification Authorities.

Each CI run creates a new test certificate. In-place upgrades across builds may require removing the previous shell and trusting the new certificate. Uninstalling an app removes its local app data; this development UI has no emulator saves, but future builds will require backup instructions and a stable release-signing identity.

APPX is the supplied format. Changing its extension to `.msix` does not port or improve the app.

## Report results

Include source commit and package version from `build-info.json`, console model/OS, deployment error code if any, and what appears after launch. Attach relevant logs after removing private paths or account details. Use this repository's bug report template. Read [frontend usage](UI.md) for themes, artwork, soundtracks and metadata behavior. Do not file this port's issues against upstream Vita3K.
