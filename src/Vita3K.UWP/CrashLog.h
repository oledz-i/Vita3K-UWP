// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>

// Writes what killed the app into vita3k-runtime.log: the exception, the
// module and offset it happened in and a short stack, or abort() and
// std::terminate. Without it a crash ended the log mid-line with no cause.
//
// Installed at startup, before the core loads: the core's guest memory fault
// filter (UwpMemoryFaultHandler.h) then chains to this one, so faults it
// recovers from never reach here and the rest do.
namespace vita3k_uwp {
void InstallCrashLog(const std::wstring& logPath);
// For a crash the app noticed itself (a XAML unhandled exception).
void WriteCrashLine(const char* text);
}
