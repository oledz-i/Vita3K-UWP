// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <Windows.h>
#include <cstdint>

namespace vita3k_uwp {
// Process-wide, as in Vita3K's existing single MemState fault callback.
// The owning core stops guest/render threads before removing this handler.
class MemoryFaultHandler {
public:
    using Callback = bool (*)(std::uint8_t*, bool) noexcept;

    static void Install(Callback callback) noexcept {
        callback_ = callback;
        if (!installed_) {
            previous_ = SetUnhandledExceptionFilter(Filter);
            installed_ = true;
        }
    }
    static void Remove() noexcept {
        if (!installed_) return;
        // Do not replace a newer filter installed by another component.
        const auto current = SetUnhandledExceptionFilter(previous_);
        if (current != Filter) SetUnhandledExceptionFilter(current);
        installed_ = false;
        callback_ = nullptr;
        previous_ = nullptr;
    }
    static LONG WINAPI Filter(EXCEPTION_POINTERS* exception) noexcept {
        if (exception && exception->ExceptionRecord) {
            const auto* record = exception->ExceptionRecord;
            if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                !(record->ExceptionFlags & EXCEPTION_NONCONTINUABLE) &&
                record->NumberParameters >= 2 &&
                (record->ExceptionInformation[0] == 0 || record->ExceptionInformation[0] == 1)) {
                auto* address = reinterpret_cast<std::uint8_t*>(record->ExceptionInformation[1]);
                if (callback_ && callback_(address, record->ExceptionInformation[0] == 1))
                    return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
        return previous_ ? previous_(exception) : EXCEPTION_CONTINUE_SEARCH;
    }
private:
    static inline Callback callback_ = nullptr;
    static inline LPTOP_LEVEL_EXCEPTION_FILTER previous_ = nullptr;
    static inline bool installed_ = false;
};
}
