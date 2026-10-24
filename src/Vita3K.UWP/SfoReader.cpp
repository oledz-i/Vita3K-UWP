// SPDX-License-Identifier: GPL-2.0-or-later
#include "SfoReader.h"
#include <cstdint>
#include <string>
#include <vector>
namespace vita3k_uwp {
namespace {
bool u16(const std::vector<std::uint8_t>& b, size_t i, std::uint16_t& v) {
    if (i > b.size() || b.size() - i < 2) return false;
    v = static_cast<std::uint16_t>(b[i] | (b[i + 1] << 8));
    return true;
}
bool u32(const std::vector<std::uint8_t>& b, size_t i, std::uint32_t& v) {
    if (i > b.size() || b.size() - i < 4) return false;
    v = b[i] | (static_cast<std::uint32_t>(b[i + 1]) << 8) |
        (static_cast<std::uint32_t>(b[i + 2]) << 16) |
        (static_cast<std::uint32_t>(b[i + 3]) << 24);
    return true;
}
std::string str(const std::vector<std::uint8_t>& b, size_t i, size_t limit) {
    if (i >= b.size()) return {};
    const size_t end = i + (limit < b.size() - i ? limit : b.size() - i);
    size_t at = i;
    while (at < end && b[at] != 0) ++at;
    return std::string(reinterpret_cast<const char*>(&b[i]), at - i);
}
}
SfoInfo ReadSfo(const std::vector<std::uint8_t>& b) {
    SfoInfo info;
    if (b.size() < 20 || b.size() > 1024 * 1024) return info;
    std::uint32_t magic=0, keyStart=0, dataStart=0, entries=0;
    if (!u32(b, 0, magic) || magic != 0x46535000 ||
        !u32(b, 8, keyStart) || !u32(b, 12, dataStart) ||
        !u32(b, 16, entries) || entries > 256 ||
        keyStart >= b.size() || dataStart >= b.size() ||
        static_cast<size_t>(entries) * 16 > b.size() - 20) return info;
    for (size_t n = 0; n < entries; ++n) {
        const size_t at = 20 + n * 16;
        std::uint16_t keyOffset=0, format=0;
        std::uint32_t dataSize=0, dataOffset=0;
        if (!u16(b, at, keyOffset) || !u16(b, at + 2, format) ||
            !u32(b, at + 4, dataSize) || !u32(b, at + 12, dataOffset)) break;
        const size_t keyAt = static_cast<size_t>(keyStart) + keyOffset;
        const size_t valueAt = static_cast<size_t>(dataStart) + dataOffset;
        if (keyAt >= b.size() || valueAt >= b.size() ||
            dataSize > b.size() - valueAt) continue;
        const auto key = str(b, keyAt, 64);
        const auto value = str(b, valueAt, dataSize);
        if (key == "TITLE" && info.title.empty()) info.title = value;
        if (key == "STITLE" && !value.empty()) info.title = value;
        if (key == "TITLE_ID") info.titleId = value;
    }
    return info;
}
} // namespace vita3k_uwp
