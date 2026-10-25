# Gameplay core port: round 2

The full-core run 40160215898 failed at SDL3-static with MSVC D8016:
`/ZW` and `/std:c++latest` are incompatible. Configuration succeeded, but this
blocked dependent Vita3K libraries. No working emulator DLL was produced.

## Applied changes

- Claude's gameplay-fix-2.zip: generated SDL projects use C++17, four build
  workers, and the build driver invokes the SDL compatibility pass.
- Also scope C++17 around SDL's source CMake directory. A regeneration keeps
  the fix; the Vita3K core retains C++23.
- Force the Windows API family header and Windows 10 target macros consistently
  via a project-include file. Disable unused Discord integration.
- Adapt remaining Boost-only generic_path() calls to native std::filesystem
  paths. Guard Win32 HWND surface creation and desktop console naming on UWP.
- Optional sparse-file/volume probes conservatively return unsupported on UWP;
  archive decryption and ordinary file caching remain present. Sparse cache
  files may occupy more storage.
- Keep guest networking on Winsock 2 instead of the desktop Winsock 1 DLL.
- Unload Vita3K renderer allocations before destroying the borrowed Vulkan
  device, including partial initialization failures.
- Capture MSVC Dxxxx and MSBuild MSBxxxx errors in ERROR-LOG.txt. D8016 was
  present in the full log but missed by the earlier summary filter.

## Sources reviewed

Onyx first: pinned revision 78f2561a16fe7a65e59893f896611263f10575c5.
[build-azahar.ps1](https://github.com/oledz-i/OnyxEMU/blob/78f2561a16fe7a65e59893f896611263f10575c5/tools/build-azahar.ps1)
disables unused frontends/backends and enables Dynarmic W^X support.
[uwp-flags.cmake](https://github.com/oledz-i/OnyxEMU/blob/78f2561a16fe7a65e59893f896611263f10575c5/tools/uwp-flags.cmake)
provides the project-include pattern for consistent API family and target macros.
The W^X port is already applied by apply-onyx-dynarmic.ps1.

Also reviewed the official
[PPSSPP CoreUWP project](https://github.com/hrydgard/ppsspp/blob/master/UWP/CoreUWP/CoreUWP.vcxproj)
and
[RetroArch UWP project](https://github.com/libretro/RetroArch/blob/master/pkg/msvc-uwp/RetroArch-msvcUWP/RetroArch-msvcUWP.vcxproj).
They separate native emulator compilation from the platform frontend rather
than enabling C++/CX across every C++ library. No emulator code from Dan Price
or SternXD was added in this round. The pre-existing SDL3 UWP library dependency
is still selected in core-candidates.lock.json; it is not an emulator core.

## Verification and limits

All source adapters were applied successfully to a fresh archive of the pinned
Vita3K libretro source. Repository and workflow syntax checks pass. These are
source-level checks; Windows CI must compile the affected targets. Xbox runtime
loading, firmware setup, Vulkan features and Undertale execution remain unverified.
