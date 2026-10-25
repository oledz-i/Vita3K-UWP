# Undertale — first PS Vita boot target

Target: **Undertale (PS Vita)**. Retail title IDs:
- North America: `PCSE01116`
- Europe: `PCSB01157`
- Japan: `PCSG01112`

Upstream Vita3K's published compatibility list marks Undertale playable, but this is **desktop emulator compatibility**, not Xbox UWP compatibility. Neither the title, Sony firmware nor game license is included in this repository or its CI builds. Only use your own legitimately obtained game/firmware files.

## What must pass, in order

1. **Core build:** `vita3k_libretro.dll` genuinely compiles for WindowsStore x64 and loads via `LoadPackagedLibrary` inside the Xbox APPX. Exported `retro_api_version`, `retro_init`, `retro_load_game`, `retro_run`, and `retro_unload_game` exist.
2. **Vita guest ARM CPU:** the pinned A32 Dynarmic JIT compiles with Onyx W^X changes for Xbox UWP and executes Vita guest code. A native `mov eax,42;ret` probe does *not* establish this.
3. **Vulkan:** the Xbox-tested Onyx-patched Mesa Dozen runtime provides a Vulkan 1.1 logical device and graphics queue. The libretro frontend must handle `RETRO_ENVIRONMENT_SET_HW_RENDER`, context negotiation and `GET_HW_RENDER_INTERFACE`; neither CPU nor game boot is possible with a fake or absent interface.
4. **Game loading:** Vita3K sees its installed official firmware under the private `vita3k` system folder, a valid input game path, and the title's correct license where required. Any PKG selected through the UWP file picker must be copied into accessible sandbox storage or served through a functioning libretro VFS; UWP's future-access token does not automatically make arbitrary external paths accessible through CRT `fopen`.
5. **First execution:** `retro_load_game` returns true and a log records Vita module loading and guest thread execution. A native executable opening is not this milestone.
6. **First game frame:** `retro_run` drives Vita emulation, libretro's Vulkan image is synchronized to the D3D12 output, and an actual Undertale frame is visible. Driver clears, test gradients and CPU-host-generated graphics do not count.
7. **Gameplay:** Undertale reaches its menu, accepts Xbox controls, enters gameplay and can write its save through `system/vita3k/ux0/user/00/savedata/<title ID>`.

An Unreal Engine, emulator UI, DOS executable or another unrelated game is **not** an alternative success criterion. The game under test is the PS Vita version of Undertale.

## Present CI work

- `scripts/patch-vita3k-uwp.py`: bypasses the two desktop Boost.Filesystem CMake calls for UWP, switches Vita filesystem paths to standard C++ filesystem and updates installer error types.
- `scripts/apply-onyx-dynarmic.ps1`: ports Onyx's JIT W^X patch set into the Vita3K Dynarmic source. Patch compatibility has passed.
- `scripts/build-vita-dynarmic-uwp.ps1`: builds just the Vita ARMv7 CPU for WindowsStore x64 without waiting for the entire emulator's package dependencies.
- `scripts/probe-vita3k-uwp.ps1`: attempts the entire libretro core. Exact configure/build failures are emitted in CI and archived.
- `onyx-dozen-integration`: separately contains the actual Onyx UWP Dozen driver and is confirmed to create a Vulkan device/queue on the user's Series S.
- `cinematic-ui-overhaul`: separately contains the catalog and the app icon/branding; does not grant game boot.

**Current status:** No verified PS Vita instructions or Undertale frames executed on Xbox. A successful build or UWP Vulkan probe is a prerequisite, not a game boot. Continue porting from the first concrete compiler error; never substitute a fake launcher or splash screen.
