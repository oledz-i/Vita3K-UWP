// SPDX-License-Identifier: GPL-2.0-or-later
#include "MenuMusic.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace vita3k_uwp {
namespace {
void append16(std::vector<std::uint8_t>& v, std::uint16_t n) {
    v.push_back(static_cast<std::uint8_t>(n));
    v.push_back(static_cast<std::uint8_t>(n >> 8));
}
void append32(std::vector<std::uint8_t>& v, std::uint32_t n) {
    append16(v, static_cast<std::uint16_t>(n));
    append16(v, static_cast<std::uint16_t>(n >> 16));
}
double note(int semitone) { return 220.0 * std::pow(2.0, semitone / 12.0); }
}
std::vector<std::uint8_t> GenerateMenuWav(unsigned track) {
    constexpr unsigned rate = 22050;
    constexpr unsigned duration = 16;
    constexpr unsigned samples = rate * duration;
    constexpr double tau = 6.283185307179586;
    // Four independent harmonic themes: Drift, Pulse, Stargaze, and Tide.
    constexpr int chords[4][4][3] = {
        {{0, 3, 7}, {-2, 3, 7}, {-4, 0, 5}, {-2, 2, 7}},
        {{0, 4, 7}, {2, 5, 9}, {-3, 0, 4}, {-5, 0, 4}},
        {{3, 7, 10}, {0, 5, 8}, {-2, 3, 7}, {0, 4, 7}},
        {{-5, 0, 4}, {-3, 0, 5}, {-2, 2, 5}, {-5, 0, 4}}
    };
    track %= 4;
    std::vector<std::uint8_t> wav;
    wav.reserve(44 + samples * 2);
    for (char c : "RIFF") { if (c) wav.push_back(static_cast<std::uint8_t>(c)); }
    append32(wav, 36 + samples * 2);
    for (char c : "WAVEfmt ") { if (c) wav.push_back(static_cast<std::uint8_t>(c)); }
    append32(wav, 16);
    append16(wav, 1);       // PCM
    append16(wav, 1);       // mono
    append32(wav, rate);
    append32(wav, rate * 2);
    append16(wav, 2);
    append16(wav, 16);
    for (char c : "data") { if (c) wav.push_back(static_cast<std::uint8_t>(c)); }
    append32(wav, samples * 2);
    for (unsigned i = 0; i < samples; ++i) {
        const double t = static_cast<double>(i) / rate;
        const unsigned chord = static_cast<unsigned>(t / 4) % 4;
        const auto& notes = chords[track][chord];
        const double slow = 0.5 - 0.5 * std::cos(tau * std::fmod(t, 4.0) / 4.0);
        double sound = 0.0;
        for (int n = 0; n < 3; ++n) {
            const double f = note(notes[n]);
            sound += std::sin(tau * f * t + n * .4) * (0.15 + 0.1 * slow);
            sound += std::sin(tau * (f * 2.0) * t) * .022;
        }
        const int arpeggio = static_cast<int>(t * (track == 1 ? 3.0 : 2.0)) % 3;
        const double bell = std::exp(-8.0 * std::fmod(t * (track == 1 ? 3.0 : 2.0), 1.0));
        sound += .18 * bell * std::sin(tau * note(notes[arpeggio] + 12) * t);
        if (track == 1) {
            sound += .09 * std::sin(tau * 55.0 * t) * std::exp(-15.0 * std::fmod(t * 2.0, 1.0));
        }
        if (track == 2) {
            sound += .075 * std::sin(tau * note(notes[0] - 12) * t);
        }
        if (track == 3) {
            sound *= 0.8 + 0.2 * std::sin(tau * .125 * t);
        }
        const double fade = std::min({1.0, t / .05, (duration - t) / .05});
        const double clamped = std::max(-1.0, std::min(1.0, sound * fade * .54));
        const auto pcm = static_cast<std::int16_t>(clamped * 32767);
        append16(wav, static_cast<std::uint16_t>(pcm));
    }
    return wav;
}
} // namespace vita3k_uwp
