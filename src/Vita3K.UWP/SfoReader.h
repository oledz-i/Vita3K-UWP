// SPDX-License-Identifier: GPL-2.0-or-later
// Safe read-only parsing for Vita param.sfo metadata; no game decryption.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace vita3k_uwp {
struct SfoInfo {
    std::string title;
    std::string titleId;
};
SfoInfo ReadSfo(const std::vector<std::uint8_t>& data);
}
