# OnyxEMU's Mesa Dozen renderer adapted for Vita3K-UWP

## What was actually reused

OnyxEMU is this project's owner's former Xbox 3DS emulator, not a generic suggestion. Its [source](https://github.com/oledz-i/OnyxEMU) includes a **UWP-compatible Mesa Dozen Vulkan driver**, real Xbox runtime diagnostics, a Direct3D 12 presenter, and a libretro Vulkan hardware-render frontend.

| Upstream component | Exact Onyx source | Vita3K adaptation |
| --- | --- | --- |
| Mesa build and UWP patches | `tools/fetch-deps.ps1`, `tools/build-dozen.ps1`, `patches/mesa/*.patch` | `scripts/build-onyx-dozen.ps1` fetches the pinned Onyx checkout and runs the **original patched build**, including its UWP Store CRT, `MESA_UWP`, and Meson sanity-test workarounds |
| Dozen ICD bootstrap | `app/ONYX3DS/Emu/VulkanHost.cpp`, `DozenDriver::Load` | `src/Vita3K.UWP/VulkanDznProbe.cpp` loads `vulkan_dzn.dll` via **`LoadPackagedLibrary`**, negotiates `vk_icdNegotiateLoaderICDInterfaceVersion`, resolves `vk_icdGetInstanceProcAddr` directly; **no desktop Vulkan loader needed** |
| GPU proof | Onyx `VulkanHost::CreateInstance` and self-tests | Creates a Vulkan 1.1 instance, enumerates physical GPUs/graphics queues, checks external-memory extensions, tries `vkCreateDevice` and `vkDeviceWaitIdle`. Shows the exact result under Developer |
| Signed packaging | Onyx's APPX packaging pattern | This repo's `.github/workflows/onyx-dozen.yml` builds the pinned Mesa driver, stages `vulkan_dzn.dll` and `dxil.dll`, builds/signs the Vita3K APPX and inspects the actual package contents |
| Presentation bridge | Onyx `VulkanHost` + `D3D12Presenter` + libretro hardware callbacks | **Not yet ported**: Onyx presents Azahar 3DS frames. Vita3K must supply its own Vulkan image through `retro_hw_render_interface_vulkan`; images must then be synchronized and transferred to the Xbox D3D12 swap chain |
| Emulated GPU & CPU | Azahar + modified Dynarmic | **Cannot copy as a Vita core**: Vita uses GXM shader/state APIs, its own kernel, game loader and guest executable format. Integrate native Vita3K libretro CPU/kernel first |

**Pinned source:** OnyxEMU commit `78f2561a16fe7a65e59893f896611263f10575c5` (October 19, 2026). Mesa patch base `a4a47003a194effad8cabe727f74985e9417476f`. Both are checked at build time against `core-candidates.lock.json`.

## Why this is better than the earlier generic Dozen attempt

Xbox UWP normally has no installed Vulkan loader. A conventional `vulkan-1.dll` loader probe was not sufficient to test Onyx's working Vulkan setup. Onyx bypasses the loader and calls Mesa's Vulkan ICD exported functions directly from the packaged DLL.

The Onyx build also addresses Windows Store-specific limitations in Mesa such as prohibited `LoadLibrary` calls, Store C runtime selection and D3D12 link imports. Our earlier generic Mesa build was not applying this proven set of patches.

The source-built driver is still subject to its actual device and extension capabilities. A 3DS Vulkan renderer working with the Onyx patches is strong evidence for the **host GPU path**, not a demonstration of Vita3K compatibility.

## Build and test

The isolated branch `onyx-dozen-integration` has a separate [GitHub Actions workflow](../.github/workflows/onyx-dozen.yml).

- `source-host-smoke`: compile and sign the Xbox app including the original Onyx ICD calling sequence; normal package does **not** contain the GPU DLL.
- `patched-dozen`: fetch exact Onyx and Mesa commits, apply Onyx's patches, compile UWP `vulkan_dzn.dll`, record DLL dependencies and retain DXIL.
- `vita3k-onyx-dozen-appx`: **only if the Mesa driver builds**, stage the two DLLs, rebuild/sign the APPX, inspect that `vulkan_dzn.dll` and `dxil.dll` really appear inside.

For an Xbox test, install the **ONYX-Dozen** artifact if the job succeeds. Open **Developer → Probe actual packaged Vulkan device**. The result should distinguish four levels: driver present; instance created; GPU enumerated; actual logical device and graphics queue created. Save the exact status text and Xbox OS version.

Neither the build pipeline nor a successful GPU probe can promise Vita games will boot. The `VitaLibretroHost` currently has only DLL ABI detection and `retro_init` scaffolding, and the full UWP Vita3K core build still has blocked UWP dependencies. The Vulkan frame lifecycle must be adapted from Onyx's libretro video frontend before `retro_load_game`/guest ARM execution can work end-to-end.

## Licensing and attribution

Original OnyxEMU source is **GPL-3.0-or-later**, including the build tooling and Onyx-derived ICD bootstrap, and is attributed in `NOTICE.md`. This branch includes `COPYING.GPLv3.txt`. Vita3K's GPL-2.0-or-later code may be combined under GPLv3 compliance. Mesa's Vulkan Dozen driver is MIT-licensed; its upstream source copyrights and notices must remain. The separately sourced DXIL DLL is distributed according to Microsoft's DirectX Shader Compiler redistribution terms.

There are **no game, firmware, key or private certificate files** in the repository or its portable test packages.

## Precise current conclusion

**Host-side work:** Onyx's actual Mesa patched source build and direct Vulkan ICD bridge have been adapted in code; Xbox compatibility of the new Vita3K package requires CI and hardware test. **Guest-side work:** game execution is **not yet integrated**; there is no legitimate claim of homebrew or commercial Vita games booting. The next decisive gate is a full UWP-built Vita3K core plus a Vulkan libretro context that delivers a real guest GPU frame.
