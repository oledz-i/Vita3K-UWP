// SPDX-License-Identifier: GPL-3.0-or-later
// Test real PS Vita guest ARM instructions through the Vita3K Dynarmic JIT.
// This is a small, legal, handwritten ARM instruction stream, not a game.
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

namespace {
constexpr std::uint32_t kBase = 0x1000;
constexpr std::uint32_t kCode[] = {
    0xE3A00007u, // mov r0, #7
    0xE2800005u, // add r0, r0, #5
    0xEF000001u, // svc #1; callback halts guest CPU
};

struct GuestMemory final : public Dynarmic::A32::UserCallbacks {
    std::array<std::uint8_t, 4096> bytes{};
    Dynarmic::A32::Jit* jit = nullptr;
    std::uint64_t ticks = 0;
    unsigned svcCalls = 0;
    bool badAddress = false;

    GuestMemory() {
        std::memcpy(bytes.data(), kCode, sizeof(kCode));
    }
    template <typename T>
    T Read(std::uint32_t address) {
        const std::uint64_t end = static_cast<std::uint64_t>(address) + sizeof(T);
        if (address < kBase || end > kBase + bytes.size()) {
            badAddress = true;
            return {};
        }
        T out{};
        std::memcpy(&out, bytes.data() + address - kBase, sizeof(out));
        return out;
    }
    template <typename T>
    void Write(std::uint32_t address, T value) {
        const std::uint64_t end = static_cast<std::uint64_t>(address) + sizeof(T);
        if (address < kBase || end > kBase + bytes.size()) {
            badAddress = true;
            return;
        }
        std::memcpy(bytes.data() + address - kBase, &value, sizeof(value));
    }
    bool PreCodeReadHook(bool, Dynarmic::A32::VAddr, Dynarmic::A32::IREmitter&) override {
        return false; // Let Dynarmic translate actual guest ARM instructions.
    }
    std::uint8_t MemoryRead8(Dynarmic::A32::VAddr a) override { return Read<std::uint8_t>(a); }
    std::uint16_t MemoryRead16(Dynarmic::A32::VAddr a) override { return Read<std::uint16_t>(a); }
    std::uint32_t MemoryRead32(Dynarmic::A32::VAddr a) override { return Read<std::uint32_t>(a); }
    std::uint64_t MemoryRead64(Dynarmic::A32::VAddr a) override { return Read<std::uint64_t>(a); }
    void MemoryWrite8(Dynarmic::A32::VAddr a, std::uint8_t v) override { Write(a, v); }
    void MemoryWrite16(Dynarmic::A32::VAddr a, std::uint16_t v) override { Write(a, v); }
    void MemoryWrite32(Dynarmic::A32::VAddr a, std::uint32_t v) override { Write(a, v); }
    void MemoryWrite64(Dynarmic::A32::VAddr a, std::uint64_t v) override { Write(a, v); }
    void InterpreterFallback(VAddr, std::size_t) override {
        if (jit) jit->HaltExecution(Dynarmic::HaltReason::UserDefined1);
    }
    void CallSVC(std::uint32_t immediate) override {
        if (immediate == 1) ++svcCalls;
        if (jit) jit->HaltExecution(Dynarmic::HaltReason::UserDefined1);
    }
    void ExceptionRaised(VAddr, Dynarmic::A32::Exception) override {
        badAddress = true;
        if (jit) jit->HaltExecution(Dynarmic::HaltReason::UserDefined1);
    }
    void AddTicks(std::uint64_t value) override { ticks += value; }
    std::uint64_t GetTicksRemaining() override {
        return ticks < 100000 ? 100000 - ticks : 0;
    }
};
}

// Returns 0 only if actual ARM guest instructions executed through Dynarmic
// and triggered the guest SVC callback, leaving guest R0 equal to 12.
// A library compiling, native x64 execution or a fake screen cannot pass it.
extern "C" __declspec(dllexport)
int __cdecl VitaGuestCpuSmoke(std::uint32_t* observedR0) {
    if (observedR0) *observedR0 = 0;
    try {
        GuestMemory memory;
        Dynarmic::A32::UserConfig config{};
        config.callbacks = &memory;
        config.arch_version = Dynarmic::A32::ArchVersion::v7;
        Dynarmic::A32::Jit jit(config);
        memory.jit = &jit;
        jit.Regs()[15] = kBase;
        jit.SetCpsr(0x10); // ARM state, user mode
        const auto halt = jit.Run();
        (void)halt;
        const auto r0 = jit.Regs()[0];
        if (observedR0) *observedR0 = r0;
        if (memory.badAddress) return -2;
        if (memory.svcCalls != 1) return -3;
        if (r0 != 12) return -4;
        return 0;
    } catch (...) {
        return -5;
    }
}
