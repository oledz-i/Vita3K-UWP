# Attribution and project identity

Vita3K-UWP is an independent project maintained at https://github.com/oledz-i/Vita3K-UWP . It is not an official Vita3K release and is not endorsed by Sony or Microsoft.

The planned emulation foundation is Vita3K, by the Vita3K team and contributors: https://github.com/Vita3K/Vita3K . The current UWP shell contains no upstream emulation core. `upstream.lock.json` records the reviewed source revision. `COPYING.txt` reproduces its GNU GPL version 2 license text.

Original shell, scripts and geometric repository banner: Copyright (C) 2026 Vita3K-UWP contributors. Licensed under GNU GPL version 2 or, at your option, any later version (SPDX: GPL-2.0-or-later). See COPYING.txt. Upstream source and third-party components retain their own copyright notices and licensing terms when integrated.

The app icons and splash screen use the upstream Vita3K logo, rasterized from `vita3k/org.vita3k.vita3k.svg` at revision `fa296cc043742692ea3a51ff0a82e5683b201f72`. The original vector is preserved at `docs/assets/vita3k-logo.svg`. The logo belongs to its upstream owners; the project maintainer reports permission to use it for this app. It is not claimed as original Vita3K-UWP artwork. The geometric repository banner remains original placeholder branding. PlayStation, PS Vita, Xbox and other product names remain the trademarks of their respective owners. No Sony or Microsoft endorsement is implied.

## OnyxEMU/Mesa Dozen Xbox renderer experiment

This branch reuses the publicly released OnyxEMU source from https://github.com/oledz-i/OnyxEMU at commit `78f2561a16fe7a65e59893f896611263f10575c5`. The separate Dozen source-build script invokes OnyxEMU's original GPL-3.0-or-later tooling and Mesa patch series (patch base `a4a47003a194effad8cabe727f74985e9417476f`), and the Vita3K Vulkan ICD startup logic was adapted from OnyxEMU `app/ONYX3DS/Emu/VulkanHost.cpp`. See `COPYING.GPLv3.txt` for the complete GPLv3 text and `COPYING.txt` for the original Vita3K GPLv2-or-later code. When distributing the combined modified GPLv2-or-later/GPLv3-or-later work, comply with GPLv3 terms; the original components retain their notices. Mesa's Dozen driver is MIT-licensed, its own notices remain in the pinned source checkout; Microsoft's DXIL DLL has separate distribution conditions from the DirectX Shader Compiler project. No proprietary PS Vita material is included.

Only the Vulkan ICD integration and Xbox UWP build workflow are reused: **Onyx's Nintendo 3DS GPU emulation and Azahar core cannot execute Vita GXM commands**. A Mesa Dozen DLL and a host Vulkan instance do not, alone, make Vita3K games boot.

## FFmpeg AAC compatibility header

`third_party/ffmpeg-internal/libavcodec/codec_internal.h` contains the FFCodec declaration from FFmpeg n6.1.1, supplied in gameplay-fix-3.zip and checked against the upstream source at https://github.com/FFmpeg/FFmpeg/tree/n6.1.1 . It retains FFmpeg's LGPL-2.1-or-later notice; the license is in `third_party/ffmpeg-internal/COPYING.LGPLv2.1`. The UWP build pins the matching FFmpeg version, and an explicit version guard rejects incompatible private layouts.
