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

std::vector<std::uint8_t> GenerateInterfaceCue(unsigned cue) {
    // Warm two-tone glass taps instead of clicks, with 3 ms attack and
    // 60-125 ms exponential release. No audio samples or copyrighted cues.
    constexpr unsigned rate = 22050;
    constexpr double tau = 6.283185307179586;
    cue %= 4;
    const double durations[4] = {0.095, 0.150, 0.130, 0.075};
    const double startFrequencies[4] = {690, 590, 490, 840};
    const double targetFrequencies[4] = {760, 900, 380, 630};
    const auto count = static_cast<unsigned>(rate * durations[cue]);
    std::vector<std::uint8_t> wav;
    wav.reserve(44 + count * 2);
    for (char c : "RIFF") if (c) wav.push_back(static_cast<std::uint8_t>(c));
    append32(wav, 36 + count * 2);
    for (char c : "WAVEfmt ") if (c) wav.push_back(static_cast<std::uint8_t>(c));
    append32(wav, 16);
    append16(wav, 1);
    append16(wav, 1);
    append32(wav, rate);
    append32(wav, rate * 2);
    append16(wav, 2);
    append16(wav, 16);
    for (char c : "data") if (c) wav.push_back(static_cast<std::uint8_t>(c));
    append32(wav, count * 2);
    double phase = 0;
    for (unsigned i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double progress = t / durations[cue];
        const double freq = startFrequencies[cue] +
            (targetFrequencies[cue] - startFrequencies[cue]) * progress;
        phase += tau * freq / rate;
        const double attack = std::min(1.0, t / 0.003);
        const double release = std::exp(-5.6 * progress);
        const double edgeFade = std::min(1.0, (durations[cue] - t) / 0.010);
        const double tone = std::sin(phase) * .72 +
            std::sin(phase * 2.005) * .16 + std::sin(phase * 3.01) * .045;
        const double sample = std::max(-1.0, std::min(1.0,
            tone * attack * release * edgeFade * 0.32));
        append16(wav, static_cast<std::uint16_t>(
            static_cast<std::int16_t>(sample * 32767)));
    }
    return wav;
}
} // namespace vita3k_uwp
