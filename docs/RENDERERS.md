# Renderer strategy

Three backend goals are tracked separately. None is implemented in this UWP port yet.

| Backend | Reviewed upstream | Windows desktop potential | Xbox UWP plan |
| --- | --- | --- | --- |
| D3D12 | No backend in the reviewed renderer source list | New implementation | Primary dedicated native backend, planned |
| Vulkan | Implemented upstream | Retain on a supported Vulkan host | No native driver path assumed; translation feasibility unproven |
| OpenGL | Implemented upstream | Retain on a supported OpenGL host | No native driver path assumed; translation feasibility unproven |

Microsoft documents D3D12 feature level **11.0** for UWP on Series X|S. API version 12 does not mean feature level 12_0/12_1. Query actual feature support, formats, shader models and resource budgets on-device. Xbox One is not the initial validation target.

## Dedicated D3D12 work

1. Prove UWP device creation, CoreWindow/SwapChainPanel presentation, resize, suspend/resume and device-loss recovery on Series S.
2. Integrate with upstream `renderer::State` and the GXM command pipeline; map draws, render targets, textures, samplers and synchronization.
3. Add shader translation appropriate to D3D12/HLSL and validate Vita-specific semantics. Existing GLSL/SPIR-V generation is useful reference material, not a complete D3D12 shader solution.
4. Implement descriptors, resource transitions, upload allocation, fences, pipeline caching, surface aliasing and readbacks.
5. Compare homebrew output with upstream; then document per-title correctness and performance with precise revisions and hardware.

A device probe or triangle is an infrastructure milestone, not a functioning emulator backend.

## Vulkan / OpenGL research

Preserve the goal of separate backend choices where feasible. A Windows desktop driver is not available to an Xbox UWP package simply because its DLL is bundled. Microsoft has described GL-on-D3D12 work for Xbox **GDK** titles; that is not evidence of a ready-made Developer Mode UWP solution. Investigate public-source translation options separately for UWP API compatibility, required GL/Vulkan extensions, shader features, synchronization, packaging, licensing and speed. ANGLE's OpenGL ES support alone is not a drop-in guarantee for upstream's desktop OpenGL backend.

Only expose a selectable renderer after runtime capability checks pass. Unsupported choices should carry an explicit reason; do not silently label a D3D12 translation path as native Vulkan or native OpenGL.

## Sources

- [Pinned upstream renderer build list](https://github.com/Vita3K/Vita3K/blob/fa296cc043742692ea3a51ff0a82e5683b201f72/vita3k/renderer/CMakeLists.txt)
- [Upstream renderer interface](https://github.com/Vita3K/Vita3K/blob/fa296cc043742692ea3a51ff0a82e5683b201f72/vita3k/renderer/include/renderer/state.h)
- [Microsoft: Xbox UWP resource and DirectX limits](https://learn.microsoft.com/en-us/previous-versions/windows/uwp/xbox-apps/system-resource-allocation)
- [DirectX team: UWP on Series X|S](https://walbourn.github.io/directx-and-uwp-on-xbox-series-x-s/)
- [Microsoft: GL-on-D3D12 for an Xbox GDK game](https://devblogs.microsoft.com/directx/opengl-on-dx12-enables-steel-assault-to-launch-on-xbox/)
