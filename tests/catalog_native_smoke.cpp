// SPDX-License-Identifier: GPL-2.0-or-later
// Portable validation of metadata parsing and original generated audio.
#include "../src/Vita3K.UWP/MenuMusic.h"
#include "../src/Vita3K.UWP/SfoReader.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

static void write16(std::vector<std::uint8_t>& b, size_t i, std::uint16_t value) {
    b[i] = static_cast<std::uint8_t>(value);
    b[i + 1] = static_cast<std::uint8_t>(value >> 8);
}
static void write32(std::vector<std::uint8_t>& b, size_t i, std::uint32_t value) {
    write16(b, i, static_cast<std::uint16_t>(value));
    write16(b, i + 2, static_cast<std::uint16_t>(value >> 16));
}
int main() {
    for (unsigned theme = 0; theme < 4; ++theme) {
        const auto sound = vita3k_uwp::GenerateMenuWav(theme);
        assert(sound.size() == 44 + 22050 * 16 * 2);
        assert(std::memcmp(sound.data(), "RIFF", 4) == 0);
        assert(std::memcmp(sound.data() + 8, "WAVEfmt ", 8) == 0);
        assert(std::memcmp(sound.data() + 36, "data", 4) == 0);
        // Generated output must include nonzero PCM audio.
        bool audible = false;
        for (size_t i = 100; i < sound.size(); ++i) {
            if (sound[i] != 0) { audible = true; break; }
        }
        assert(audible);
        if (theme > 0) {
            assert(sound != vita3k_uwp::GenerateMenuWav(0));
        }
    }

    // Build one minimal valid PSF with TITLE and TITLE_ID.
    std::vector<std::uint8_t> sfo(128, 0);
    write32(sfo, 0, 0x46535000);
    write32(sfo, 8, 52); // string table offset
    write32(sfo, 12, 80); // value table offset
    write32(sfo, 16, 2);  // entries
    write16(sfo, 20, 0);  // TITLE string key
    write16(sfo, 22, 0x0204);
    write32(sfo, 24, 10);
    write32(sfo, 32, 0);  // value offset 0
    write16(sfo, 36, 6);  // TITLE_ID key offset
    write16(sfo, 38, 0x0204);
    write32(sfo, 40, 10);
    write32(sfo, 48, 16); // next data at 96
    std::memcpy(sfo.data() + 52, "TITLE", 6);
    std::memcpy(sfo.data() + 58, "TITLE_ID", 9);
    std::memcpy(sfo.data() + 80, "Test Game", 10);
    std::memcpy(sfo.data() + 96, "PCSE00001", 10);
    auto info = vita3k_uwp::ReadSfo(sfo);
    assert(info.title == "Test Game");
    assert(info.titleId == "PCSE00001");
    sfo[0] = 0xff;
    info = vita3k_uwp::ReadSfo(sfo);
    assert(info.title.empty());
    assert(info.titleId.empty());

    std::cout << "Vita SFO parsing and all four original soundtracks: PASS\n";
}
