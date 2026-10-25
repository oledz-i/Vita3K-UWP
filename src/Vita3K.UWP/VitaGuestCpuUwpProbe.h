// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
// Calls a signed packaged DLL containing the ACTUAL Vita3K ARMv7 Dynarmic
// recompiler. The return value is meaningful only after execution on Xbox.
std::wstring RunVitaGuestArmCpuProbe();
