# Vita3K upstream assessment

Reviewed on **2026-10-24** against [Vita3K/Vita3K at fa296cc043742692ea3a51ff0a82e5683b201f72](https://github.com/Vita3K/Vita3K/tree/fa296cc043742692ea3a51ff0a82e5683b201f72). `upstream.lock.json` records the exact baseline. The emulation source is not vendored or linked into the development shell yet.

## Source map and findings

All paths below refer to that pinned revision.

| Source | Finding and port implication |
| --- | --- |
| `CMakeLists.txt` | CMake minimum 3.22; C++23 required. Port the real dependency graph rather than wrapping a Windows EXE. |
| `vita3k/CMakeLists.txt` | Desktop builds include `gui-qt`, Qt translations and `sdl-frontend`; Android has separate entry points. UWP needs its own platform entry/lifecycle boundary. |
| `vita3k/renderer/CMakeLists.txt` | Explicit `src/gl` and `src/vulkan` implementations. No D3D12 source list present. |
| `vita3k/renderer/include/renderer/state.h` | Shared renderer abstraction covers frame rendering, presentation, caches, memory mapping, filters and more; a D3D12 backend is substantial core work. |
| `vita3k/cpu/src/cpu.cpp`, `dynarmic_cpu.cpp` | Dynarmic CPU path. Audit executable-memory allocation, exception handling and supported UWP APIs before assuming the JIT works. |
| `vita3k/mem/src/mem.cpp` | Windows virtual-memory reservation, commitment and protection calls. Validate AppContainer availability, large address reservations and actual console budgets. |
| `vita3k/shader`, `vita3k/gxm` | Shader translation and Vita GPU commands are core integration points for a new graphics backend. |
| `vita3k/io`, `audio`, `input`, `touch` | Audit sandboxed filesystem access, audio initialization, gamepad mapping and front/rear touch emulation. |
| `.gitmodules`, `external/`, `vcpkg.json` | Dependencies require recursive checkout and individual UWP/license review; desktop success does not establish console support. |

## Proposed boundaries

Keep the portable emulation core separate from the UWP host. The host should own activation, suspend/resume, controller interaction, storage permissions and presentation. Preserve upstream history and attribution when importing core code; record the baseline and keep platform patches reviewable. Disable or replace desktop-only updater, frontend and optional integration code where it relies on unavailable APIs.

Do not assume the latest SDL or Qt desktop frontend is compatible with Xbox UWP. Their versions and platform support need explicit investigation. The bootstrap shell intentionally does not pull in the full upstream dependency tree.

## Feasibility gates before compatibility promises

1. Launch the package and exercise suspend/resume on real Series S hardware.
2. Compile selected core dependencies for x64 AppContainer; record unsupported APIs.
3. Prove executable-memory/JIT and exception paths with a small diagnostic; measure real available memory outside the debugger.
4. Prove the D3D12 presentation path and feature level; then implement renderer semantics.
5. Boot a legally obtained homebrew test with logging; expand only after reproducible correctness.

The dedicated Vulkan and OpenGL wishes remain tracked in [RENDERERS.md](RENDERERS.md). They cannot currently be promised as native Xbox backends. No performance estimate is justified before these gates.

## License and provenance

Upstream's README describes GPLv2; inspected source headers allow version 2 or later. Its third-party dependencies have separate terms. `COPYING.txt` is copied unchanged from the reviewed upstream revision. New original files in this repository are offered under GPL-2.0-or-later. Preserve per-file headers and collect dependency notices when actual core/dependency code is integrated. No proprietary console SDK, firmware, game content or keys are included.
