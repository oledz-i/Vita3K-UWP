// SPDX-License-Identifier: GPL-2.0-or-later
// Four original synthesized, loopable menu soundtracks. No third-party audio.
#pragma once
#include <cstdint>
#include <vector>
namespace vita3k_uwp {
std::vector<std::uint8_t> GenerateMenuWav(unsigned track);
// Short, original UI cues: focus (0), select (1), back (2), shelf swipe (3),
// page change (4) and confirm shimmer (5).
constexpr unsigned kInterfaceCueCount = 6;
std::vector<std::uint8_t> GenerateInterfaceCue(unsigned cue);
}
