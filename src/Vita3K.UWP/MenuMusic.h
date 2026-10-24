// SPDX-License-Identifier: GPL-2.0-or-later
// Four original synthesized, loopable menu soundtracks. No third-party audio.
#pragma once
#include <cstdint>
#include <vector>
namespace vita3k_uwp {
std::vector<std::uint8_t> GenerateMenuWav(unsigned track);
// Short, original UI cues: focus (0), confirm (1), back (2), shelf swipe (3).
std::vector<std::uint8_t> GenerateInterfaceCue(unsigned cue);
}
