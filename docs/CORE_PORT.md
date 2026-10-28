# Real Vita3K Core Port: Status and Vulkan/Dozen Work

## Goal

Boot an **actual, redistributable Vita homebrew**, with ARM guest instructions executing through upstream Vita3K, on Xbox Series X|S **Developer Mode**. The same core should eventually support the legitimately owned commercial-game formats that upstream Vita3K can handle. This is the only game-boot milestone that counts.

**Current result: not achieved.** The package can scan file headers and submit a D3D12 test frame; neither action executes Vita software.

## Core chosen: Vita3K libretro fork

The current Xbox `core-integration` branch pins both projects in [core-candidates.lock.json](../core-candidates.lock.json):

- Original emulator: https://github.com/Vita3K/Vita3K (the previously pinned code whose bitmap allocator is already compiled and executed in UWP).
- Libretro emulator core: https://github.com/WizzardSK/vita3k-libretro, which incorporates upstream Vita3K and implements a host-friendly `libretro` C ABI for game loading, callbacks and firmware management.

Unlike a source list or simulated "Boot Game" screen, this is an **existing emulator** with a proven desktop/Linux/Android game execution path. The libretro port supports game paths for VPK, ZIP, PKG, extracted folders/`eboot.bin`, owns the Vita emulated filesystem and built-in assets, and requires official PS Vita firmware to be installed.

The Windows x64 `vita3k_libretro.dll` released for RetroArch is a **desktop binary**. Microsoft documents that desktop native DLLs cannot simply be assumed valid for the restricted UWP runtime. It must be **rebuilt as a Windows Store-compatible module** and audited for allowed APIs and dependencies.

## What was added to this Xbox project

- `scripts/probe-vita3k-uwp.ps1` tries to configure/build the **actual pinned full libretro Vita3K sources and submodules** using Visual Studio `WindowsStore` and `x64-uwp` dependencies. It records the precise configure/build error in CI artifacts instead of quietly falling back to a desktop target.
- `src/Vita3K.UWP/VitaLibretroHost.cpp` loads a **signed APPX-packaged DLL only** using `LoadPackagedLibrary`, validates `retro_api_version` and genuine Vita3K `retro_*` exports, registers minimal environment/audio/input callbacks, and can call the real `retro_init`. It refuses to invent a Vulkan context or claim a frame was emulated.
- `src/Vita3K.UWP/VulkanDznProbe.cpp` inspects whether the package contains a Vulkan loader, supports Vulkan >= 1.1, creates a Vulkan instance and enumerates physical devices without requiring a Win32 `HWND`. It does not embed Mesa binaries or assume Dozen is installed.
- The **Developer** page has independent buttons for the libretro probe/initialization, packaged Vulkan device detection, and the existing upstream bitmap allocator and D3D12 test.

In a development build that does **not** contain a native UWP Vita3K DLL, the new core-probe button is expected to say **not packaged**. This is a truthful blocked state, not a game boot.

## Reusing the OnyxEMU UWP Vulkan renderer

The experimental `onyx-dozen-integration` branch pins the owner's original [OnyxEMU](https://github.com/oledz-i/OnyxEMU) code and exact Mesa patch series, rather than trying only stock Mesa. Its Windows CI invokes Onyx's original `fetch-deps.ps1` and `build-dozen.ps1` to generate real UWP `vulkan_dzn.dll` and stage the required `dxil.dll`.

The Vita frontend now follows Onyx's `DozenDriver` **direct ICD bootstrap**: load the packaged DLL, negotiate the ICD interface, get `vkCreateInstance`, enumerate physical devices and attempt a Vulkan logical device and graphics queue. **This does not yet feed Vita3K's GXM frames into D3D12.** Detailed boundaries, verification steps and licensing are in [Onyx GPU port](ONYX_GPU_PORT.md).

## Why Mesa Dozen (dzn) is promising, but not yet the answer

- **Mesa Dozen** implements **Vulkan over Direct3D 12**. The relevant Windows binary generally consists of Mesa `vulkan_dzn.dll` (an ICD), an appropriate Vulkan loader and support DLLs.
- Vita3K libretro requires **Vulkan 1.1 or newer** and a libretro frontend that supplies the Vulkan hardware-render context and frame-lifecycle callbacks, or OpenGL with `glcore`.
- Xbox UWP provides Direct3D 12 graphics, but its XAML `SwapChainPanel` is not a Win32 `HWND`. We must either integrate Mesa's Vulkan device with an offscreen rendered image transferred into the current D3D12 host, or provide another supported UWP Vulkan present mechanism.
- The desktop Dozen DLL may import APIs that do not exist in UWP AppContainer. A desktop `vulkan_dzn.dll` is **not automatically suitable** for Xbox.
- Vulkan instance creation alone does not prove the **device extensions**, shader-interlock behavior, SPIR-V shader translation, surface synchronization, or guest memory-sharing methods required by Vita3K.

Upstream Mesa information: https://docs.mesa3d.org/ . Khronos lists Dozen as Vulkan-on-DX12: https://dev.vulkan.org/porting . UWP DLL requirements: https://learn.microsoft.com/en-us/cpp/porting/how-to-use-existing-cpp-code-in-a-universal-windows-platform-app .

## First real UWP compilation result and JIT evidence

The first complete source checkout for the pinned Vita3K libretro fork **succeeded** on Windows CI. Its initial Windows Store CMake attempt failed **before any Vita CPU or renderer source was built** because vcpkg marked `boost-filesystem` as `!uwp`. This is a real dependency restriction, not a Vita3K runtime crash. The follow-up CI experiment explicitly passes `VCPKG_INSTALL_OPTIONS=--allow-unsupported`; an unsupported package must still compile and function to count as progress. A successful Windows desktop build or forced vcpkg install is **not** evidence of Xbox compatibility.

From the working UWP Dynarmic port in `katakishi0/eden-xbox`, two important Xbox preconditions emerged:

- Declare `<Capability Name="codeGeneration" />` in the APPX manifest. The integration build now does so.
- Use UWP `VirtualAllocFromApp` / `VirtualProtectFromApp` with writable memory becoming readable+executable (W^X); do **not** assume desktop `VirtualProtect` works in an Xbox sandbox.

The **Developer** page now includes an **on-demand generated x64 machine-code test**: it allocates a page using the *FromApp* APIs, emits the small native x64 `mov eax,42; ret` function, changes the page to RX and calls the machine code to see whether it returns 42. This runs **only** if the user explicitly selects the test. It is not Vita guest code or Dynarmic, and actual Xbox execution has not been observed yet. An Xbox code-integrity denial could terminate the app; begin with the nonexecuting memory test.

A separate precedent for Mesa-on-Xbox exists in the `katakishi0/eden-xbox` and `XboxEmuPorts/Cemu-UWP` codebases. **Graphics version mismatch is a hard gate:** Vita3K documents OpenGL **4.4** as its minimum, whereas stock Mesa's D3D12 Gallium driver documents OpenGL **3.3**. Consequently, **stock Mesa D3D12 OpenGL is not a working Vita3K fallback** without significant OpenGL-version/extension work. Dozen Vulkan-on-D3D12 is the more promising unmodified renderer path, although it must actually enumerate a UWP device and implement Vita3K's required capabilities.

## Remaining technical gates

1. **Compile actual Vita3K for UWP:** ensure SDL3, Dynarmic's JIT and allocator, emulated kernel, FFmpeg, libcurl, OpenSSL, shader libraries and the Vita filesystem build and link for Xbox's Windows Store API family.
2. **Initialize core and guest system:** provide writable `Vita3KSystem` storage, real firmware installation and `libretro` environment/VFS callbacks. Core probe/initialize is not enough: a Vita module must run guest instructions.
3. **Vulkan-on-D3D12:** build and package a correct UWP-compatible Vulkan loader and Mesa Dozen ICD or implement the Vita GXM rendering backend in D3D12. Negotiate `RETRO_HW_CONTEXT_VULKAN` properly and transfer the image to a `SwapChainPanel` surface.
4. **Homebrew proof:** load a small VitaSDK homebrew as a legal test case, record `retro_load_game` success, logged emulated CPU entry, Vita kernel service calls, and video output on a Series S. A real game program must execute, not a host-generated image.
5. **Commercial games:** when the same core boots homebrew, support games/updates/DLC from user-owned dumps and applicable licenses using the upstream installed/direct game loader. Do not bundle proprietary system files.

## Xbox hardware test instructions

Install the normal signed UWP integration APPX only after the **Windows package job** has passed. The Vulkan/core probes are safe read-only checks. A missing core, failed UWP API import, absent Vulkan device or failed D3D12 presentation should be reported with the exact error text. The currently linked Vita3K bitmap allocator is not a replacement for the full core.

The CI UWP source attempt may fail, especially on third-party packages that only ship desktop Windows targets. Inspect its **`vita3k-full-core-uwp-port-probe`** artifact to identify the first actual blocking package or compile/link API. Do not conflate successful desktop core builds with a successful UWP port. CI does not have an Xbox GPU and cannot prove gameplay.
