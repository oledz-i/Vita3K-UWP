// SPDX-License-Identifier: GPL-2.0-or-later
#include "UwpJitProbe.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>

std::wstring RunUwpJitLivenessProbe() {
#if !defined(_M_X64)
    return L"JIT liveness probe supports only x64. No instructions executed.";
#else
    // Reserve and commit a single W^X code page with UWP's *FromApp API.
    constexpr SIZE_T codeBytes = 4096;
    void* page = VirtualAllocFromApp(nullptr, codeBytes, MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE);
    if (!page) {
        return L"VirtualAllocFromApp(RW) failed: Windows error " +
            std::to_wstring(GetLastError());
    }

    // Independent native CPU smoke: MOV EAX, 42; RET.
    // This code is NOT PS Vita ARM emulation, not a Dynarmic JIT.
    constexpr std::uint8_t instructions[6] = {
        0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3
    };
    std::memcpy(page, instructions, sizeof(instructions));
    DWORD original = 0;
    if (!VirtualProtectFromApp(page, codeBytes, PAGE_EXECUTE_READ, &original)) {
        const DWORD error = GetLastError();
        VirtualFree(page, 0, MEM_RELEASE);
        return L"VirtualProtectFromApp(RW->RX) denied: Windows error " +
            std::to_wstring(error) +
            L". Check manifest codeGeneration and Xbox app execution mode.";
    }
    if (!FlushInstructionCache(GetCurrentProcess(), page, sizeof(instructions))) {
        const DWORD error = GetLastError();
        DWORD ignored = 0;
        VirtualProtectFromApp(page, codeBytes, PAGE_READWRITE, &ignored);
        VirtualFree(page, 0, MEM_RELEASE);
        return L"FlushInstructionCache failed: Windows error " + std::to_wstring(error);
    }

    // Invoke code ONLY if the user explicitly clicked the JIT test button.
    // If the Xbox runtime rejects executable pages at the CPU level, an OS
    // code-integrity termination could still end the app: report this case.
    using NoArgs = int(__cdecl*)();
    const int result = reinterpret_cast<NoArgs>(page)();

    DWORD ignored = 0;
    VirtualProtectFromApp(page, codeBytes, PAGE_READWRITE, &ignored);
    VirtualFree(page, 0, MEM_RELEASE);
    if (result == 42) {
        return L"PASS: actual generated x64 machine code returned 42 from a UWP W^X page. "
               L"This is a JIT prerequisite, not Vita ARM CPU execution.";
    }
    return L"FAIL: generated machine code returned " + std::to_wstring(result) +
           L" (expected 42).";
#endif
}
