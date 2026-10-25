# Roadmap

No game-performance or release-date guarantees. The `core-integration` branch remains experimental; Windows CI builds do **not** prove Xbox hardware support.

| Milestone | Exit criterion | Current state |
| --- | --- | --- |
| 0 — Repository and CI | Reproducible development APPX workflow | **Passed:** builds and signs x64 package on GitHub Actions |
| 1 — Xbox hardware | Install, launch, suspend/resume and test USB file pickers | **Not verified on console** |
| 2 — Core integration | Initialize upstream Dynarmic CPU + memory, Vita kernel and loader | **Partial:** authentic Vita3K bitmap allocator linked and tested; VM probes exist, no Dynarmic or Vita kernel |
| 3 — D3D12 GXM renderer | Execute Vita GXM draw commands with correctly translated shaders and surfaces | **Partial:** Xbox-native D3D12 host, GPU clear and BGRA framebuffer presentation; **no Vita GXM draw pipeline** |
| 4 — Library experience | Home/Library/Settings pages with artwork, sounds, storage, controller navigation | **Implemented for desktop compilation:** five themes, four menu tracks, four UI cues, custom/SteamGridDB artwork, input-format checks; Xbox UI tests pending |
| 5 — First homebrew boot | Load a decrypted Vita homebrew ELF/SELF, resolve imports, run CPU/kernel and produce a displayed frame | **Not achieved:** requires complete core, loader, service stubs and GXM shader execution |
| 6 — Commercial-game capability | Legitimate license handling, install/launch flow, compatibility reporting | **Not achieved:** file/PKG header checks and stored license-file permissions only; no decryption/installation or boot |
| Parallel — Vulkan/OpenGL | Documented Xbox viable graphics paths | Research only; neither supported natively by Xbox UWP |
| Release readiness | Persistent signing, tested upgrades, saves, hardware compatibility; Tools page hidden in public builds ([how](RELEASING.md)) | Pending |

## Next technical gates

1. **CPU and memory:** compile pinned Dynarmic for the Xbox UWP toolchain, verify writable/executable permissions and 4 GiB guest address mapping on the physical console.
2. **Vita filesystem and loader:** create a user-selectable Vita storage root, support safe archive/package installation, load Vita executable modules and resolve library imports.
3. **Kernel and HLE:** integrate Vita3K threading, syscalls, filesystem, input, timer and module dispatch used by a minimal VitaSDK homebrew.
4. **GPU rendering:** translate GXM state, shader programs, vertex/index buffers, texture surfaces and synchronization into real D3D12 resources. The present BGRA uploader is a test surface, not this backend.
5. **First actual homebrew run:** pick one legally redistributable test homebrew, log CPU/kernel startup, see first frame, collect audio/input results on Xbox.
6. **Commercial titles:** only after the same core works, support user-owned dumps and license handling; do not assume a Vita3K desktop-compatible game is compatible with the Xbox port.

The UI's **Inspect launch readiness** button validates file headers and shows missing dependencies; it does **not** execute code. Failed or blocked game launches must never be presented as successes.
