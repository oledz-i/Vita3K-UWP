// SPDX-License-Identifier: GPL-3.0-or-later
#include "VitaGuestCpuUwpProbe.h"
#include <windows.h>
#include <cstdint>
#include <string>

std::wstring RunVitaGuestArmCpuProbe() {
#if !defined(_M_X64)
    return L"Vita guest ARM CPU test needs an x64 host.";
#else
    HMODULE module = LoadPackagedLibrary(L"vita_guest_cpu_probe.dll", 0);
    if (!module) {
        return L"Vita guest ARM CPU library not packaged (Windows error " +
            std::to_wstring(GetLastError()) + L").";
    }
    using ExecuteGuestArm = int(__cdecl*)(std::uint32_t*);
    auto fn = reinterpret_cast<ExecuteGuestArm>(
        GetProcAddress(module, "VitaGuestCpuSmoke"));
    if (!fn) {
        FreeLibrary(module);
        return L"Packaged Dynarmic guest ARM test DLL has no VitaGuestCpuSmoke export.";
    }
    std::uint32_t guestR0 = 0;
    const int result = fn(&guestR0);
    FreeLibrary(module);
    if (result == 0 && guestR0 == 12) {
        return L"PASS: Vita3K Dynarmic executed ARMv7 MOV 7, ADD 5 and SVC; "
               L"guest R0=12. This is guest ARM CPU execution, not an Undertale boot.";
    }
    return L"Vita Dynarmic guest CPU test failed (result=" +
        std::to_wstring(result) + L", guest R0=" + std::to_wstring(guestR0) +
        L"). Check Xbox JIT permissions and core diagnostics.";
#endif
}
