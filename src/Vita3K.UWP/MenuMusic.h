// SPDX-License-Identifier: GPL-2.0-or-later
// Four original synthesized, loopable menu soundtracks. No third-party audio.
#pragma once
#include <cstdint>
#include <vector>
namespace vita3k_uwp {
std::vector<std::uint8_t> GenerateMenuWav(unsigned track);
}
