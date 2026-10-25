#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Disable unsupported desktop host backends in a pinned Vita3K UWP checkout.

The libretro emulator uses its own audio callbacks. WinMM cubeb and desktop
Windows OS-version detection are not necessary for running Vita guest code.
Changes are applied only to a checked-out source copy by CI.
"""
from __future__ import annotations
import pathlib
import sys

if len(sys.argv) != 2:
    raise SystemExit("Usage: patch-vita3k-uwp-deps.py <pinned-upstream-core>")
root = pathlib.Path(sys.argv[1]).resolve()

def replace_once(path: str, before: str, after: str) -> None:
    filename = root / path
    content = filename.read_text()
    count = content.count(before)
    if count != 1:
        raise RuntimeError(f"{path}: expected one upstream section, got {count}")
    filename.write_text(content.replace(before, after))

replace_once(
    "external/CMakeLists.txt",
    "add_subdirectory(cubeb EXCLUDE_FROM_ALL)",
    """if(BUILD_LIBRETRO AND CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    # Xbox libretro audio is passed to the frontend, not the WinMM backend.
    add_library(cubeb INTERFACE)
else()
    add_subdirectory(cubeb EXCLUDE_FROM_ALL)
endif()"""
)
replace_once(
    "vita3k/audio/CMakeLists.txt",
    """if(BUILD_LIBRETRO)
    add_library(
        audio
        STATIC
        src/audio.cpp
        src/impl/cubeb_audio.cpp
        src/impl/libretro_audio.cpp)""",
    """if(BUILD_LIBRETRO)
    set(VITA3K_LIBRETRO_CUBEB_SOURCE src/impl/cubeb_audio.cpp)
    if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
        set(VITA3K_LIBRETRO_CUBEB_SOURCE "")
    endif()
    add_library(
        audio
        STATIC
        src/audio.cpp
        src/impl/libretro_audio.cpp
        @CUBEB_SOURCE@)""".replace("@CUBEB_SOURCE@", "${VITA3K_LIBRETRO_CUBEB_SOURCE}")
)
replace_once(
    "vita3k/audio/src/audio.cpp",
    """    } else if (adapter_name == "Cubeb") {
        adapter = std::make_unique<CubebAudioAdapter>(*this);
    } else {
        LOG_ERROR("Unknown audio adapter {}", adapter_name);
        return;
    }
#else""",
    """#ifndef VITA3K_UWP
    } else if (adapter_name == "Cubeb") {
        adapter = std::make_unique<CubebAudioAdapter>(*this);
#endif
    } else {
        LOG_ERROR("Unknown audio adapter {}", adapter_name);
        return;
    }
#else"""
)
replace_once(
    "external/CppCommon/source/environment.cpp",
    "#elif defined(_WIN32) || defined(_WIN64)\n    static NTSTATUS",
    """#elif defined(VITA3K_UWP)
    // Host version string is informational; UWP does not expose desktop
    // GetSystemMetrics and arbitrary module APIs.
    return "Windows UWP";
#elif defined(_WIN32) || defined(_WIN64)
    static NTSTATUS"""
)
print("UWP libretro audio: removed WinMM; kept libretro audio callback backend.")
print("CppCommon host OS detection: bypassed desktop-only version calls.")
