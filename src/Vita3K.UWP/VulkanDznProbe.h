// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal Khronos Vulkan 1.1 *ABI probe* using an APPX-packaged Vulkan loader.
// No Mesa Dozen binaries are bundled or inferred from D3D12 availability.
#pragma once
#include <string>
std::wstring ProbePackagedVulkan();
