#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Experimental, isolated port adapter for real Vita3K libretro sources.

Only patches a fresh pinned checkout under upstream-core/. It never replaces
original code with fake emulator functions. std::filesystem is an experiment
to remove Boost.Filesystem's desktop-only Win32 API use from a UWP build.
"""
from __future__ import annotations

import json
import pathlib
import re
import sys

if len(sys.argv) != 2:
    raise SystemExit("Usage: patch-vita3k-uwp.py <pinned-vita3k-source-directory>")
source = pathlib.Path(sys.argv[1]).resolve()
if not (source / "vita3k/util/include/util/fs.h").is_file():
    raise SystemExit("Missing real upstream Vita3K filesystem header")

manifest_file = source / "vcpkg.json"
manifest = json.loads(manifest_file.read_text())
removed = {"boost-filesystem"}
existing = set(x for x in manifest["dependencies"] if isinstance(x, str))
assert "boost-filesystem" in existing, existing
manifest["dependencies"] = [
    dep for dep in manifest["dependencies"]
    if not isinstance(dep, str) or dep not in removed
]
manifest_file.write_text(json.dumps(manifest, indent=2) + "\n")

cmake = source / "CMakeLists.txt"
text = cmake.read_text()
assert text.count("\nget_boost()\n") == 1, "Upstream Boost integration moved"
text = text.replace(
    "\nget_boost()\n",
    "\nif(CMAKE_SYSTEM_NAME STREQUAL \"WindowsStore\")\n"
    "  # Port experiment: use std::filesystem, not the desktop-only Boost.Filesystem.\n"
    "  add_compile_definitions(VITA3K_UWP=1)\n"
    "  find_package(Boost 1.81 REQUIRED)\n"
    "  set(Boost_LIBRARIES \"\")\n"
    "else()\n"
    "  get_boost()\n"
    "endif()\n",
)
cmake.write_text(text)

filesystem = source / "vita3k/util/include/util/fs.h"
original = filesystem.read_text()
old = (
    "#include <boost/filesystem.hpp>\n"
    "#include <boost/filesystem/fstream.hpp>"
)
new = (
    "#ifdef VITA3K_UWP\n"
    "#include <filesystem>\n"
    "#include <fstream>\n"
    "namespace fs {\n"
    "using namespace std::filesystem;\n"
    "using ifstream = std::ifstream;\n"
    "using ofstream = std::ofstream;\n"
    "using fstream = std::fstream;\n"
    "}\n"
    "#else\n"
    "#include <boost/filesystem.hpp>\n"
    "#include <boost/filesystem/fstream.hpp>\n"
    "#endif"
)
assert old in original, "Vita3K util filesystem includes moved"
original = original.replace(old, new)
assert "namespace fs = boost::filesystem;" in original
original = original.replace(
    "namespace fs = boost::filesystem;",
    "#ifndef VITA3K_UWP\nnamespace fs = boost::filesystem;\n#endif"
)
assert "struct fmt::formatter<boost::filesystem::path> : ostream_formatter {};" in original
original = original.replace(
    "struct fmt::formatter<boost::filesystem::path> : ostream_formatter {};",
    "struct fmt::formatter<fs::path> : ostream_formatter {};"
)
filesystem.write_text(original)

print("Applied experimental genuine Vita3K WindowsStore std::filesystem port patch.")
print(f"Removed manifest dependencies: {', '.join(sorted(existing & removed))}")
print("Emulator CPU, Vita kernel, package loader, GXM, and graphics code kept intact.")

# The nested build invokes get_boost() again; do not reintroduce filesystem.
nested = source / 'vita3k/CMakeLists.txt'
text = nested.read_text()
assert text.count('\nget_boost()\n') == 1
nested.write_text(text.replace('\nget_boost()\n', '\nif(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")\n  get_boost()\nendif()\n'))

# Match the standard filesystem error_code type at its actual call sites.
for relative in [
    'config/src/settings.cpp', 'config/src/config.cpp', 'io/src/io.cpp',
    'libretro/libretro_game_loader.cpp', 'libretro/libretro_vita3k.cpp',
    'libretro/libretro_lazy_pkg.cpp', 'libretro/libretro_installer.cpp',
]:
    path = source / 'vita3k' / relative
    text = path.read_text()
    assert 'boost::system::error_code' in text, relative
    text = text.replace('boost::system::error_code', 'std::error_code')
    text = '#include <system_error>\n' + text
    # Trophy refresh hashes the timestamp. std::filesystem returns a clock point.
    if relative == 'libretro/libretro_vita3k.cpp':
        assert 'static_cast<long long>(stamp)' in text
        text = text.replace('static_cast<long long>(stamp)', 'static_cast<long long>(stamp.time_since_epoch().count())')
    path.write_text(text)
parent = source / 'vita3k/modules/module_parent.cpp'
text = parent.read_text().replace('#include <boost/filesystem/operations.hpp>', '#include <util/fs.h>')
parent.write_text(text)
print('Adapted nested Boost discovery, filesystem error codes and trophy timestamp handling.')

# The libretro backend delivers guest audio to the frontend. Do not build
# the desktop Cubeb fallback or include its desktop-only headers for Xbox.
audio_cmake = source / 'vita3k/audio/CMakeLists.txt'
text = audio_cmake.read_text()
first, rest = text.split('else()', 1)
assert 'src/impl/cubeb_audio.cpp' in first and 'util cubeb kernel' in first
first = first.replace('        src/impl/cubeb_audio.cpp\n', '').replace('util cubeb kernel', 'util kernel')
audio_cmake.write_text(first + 'else()' + rest)
audio_source = source / 'vita3k/audio/src/audio.cpp'
text = audio_source.read_text()
text = text.replace('#include <audio/impl/cubeb_audio.h>', '#ifndef VITA3K_UWP\n#include <audio/impl/cubeb_audio.h>\n#endif')
start = text.index('#ifdef BUILD_LIBRETRO', text.index('void AudioState::set_backend'))
end = text.index('#else', start)
block = text[start:end]
old = '    } else if (adapter_name == "Cubeb") {\n        adapter = std::make_unique<CubebAudioAdapter>(*this);'
assert old in block
block = block.replace(old, '')
text = text[:start] + block + text[end:]
audio_source.write_text(text)
external = source / 'external/CMakeLists.txt'
text = external.read_text()
old = 'add_subdirectory(cubeb EXCLUDE_FROM_ALL)'
assert old in text
external.write_text(text.replace(old, 'if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")\n  '+old+'\nendif()'))
print('Routed Xbox guest audio exclusively through the libretro frontend backend.')
text = external.read_text()
start = text.index('if (ARCHITECTURE STREQUAL "arm64")', text.index('add_subdirectory(cubeb'))
end = text.index('\nif(ANDROID)', start)
text = text[:start] + 'if(TARGET speex)\n' + text[start:end] + '\nendif()\n' + text[end:]
external.write_text(text)

# Match the guest address-space allocations to the Xbox AppContainer APIs,
# independently of Dynarmic's already-patched executable code-cache allocator.
memory = source / 'vita3k/mem/src/mem.cpp'
text = memory.read_text()
assert '#include <Windows.h>' in text
text = text.replace('#include <Windows.h>', '#include <Windows.h>\n#ifdef VITA3K_UWP\n#define VirtualAlloc VirtualAllocFromApp\n#define VirtualProtect VirtualProtectFromApp\n#endif', 1)
memory.write_text(text)
print('Guest address-space allocation/protection now uses the FromApp APIs on UWP.')

# Upstream ffmpeg-core downloads desktop static libraries and links user32,
# psapi, and other desktop dependencies. Build the codecs for x64-uwp instead.
manifest = json.loads(manifest_file.read_text())
manifest['dependencies'].append({'name': 'ffmpeg', 'default-features': False,
    'features': ['avcodec', 'avdevice', 'avfilter', 'avformat', 'swresample', 'swscale']})
# Preserve the older codec API used by this pinned Vita3K source.
manifest.setdefault('overrides', []).append({'name': 'ffmpeg', 'version': '6.1.1', 'port-version': 11})
manifest_file.write_text(json.dumps(manifest, indent=2) + '\n')
text = external.read_text()
old = 'add_subdirectory(ffmpeg)'
assert text.count(old) == 1
text = text.replace(old, '''if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
  find_package(FFMPEG REQUIRED)
  add_library(ffmpeg INTERFACE)
  target_include_directories(ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
  target_link_directories(ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
  target_link_libraries(ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
else()
  add_subdirectory(ffmpeg)
endif()''')
external.write_text(text)
print('Codec dependencies use source-built UWP FFmpeg 6.1.1 instead of desktop binaries.')

# OSVersion is host logging metadata. Do not probe desktop system DLLs from
# an AppContainer merely to decorate a log line.
environment = source / 'external/CppCommon/source/environment.cpp'
text = environment.read_text()
old = 'std::string Environment::OSVersion() {\n#if defined(__APPLE__)'
assert old in text
text = text.replace(old, 'std::string Environment::OSVersion() {\n#if defined(VITA3K_UWP)\n    return "Windows UWP";\n#elif defined(__APPLE__)', 1)
environment.write_text(text)

# Pin at the source level too, so a CMake regeneration cannot bring back
# C++23 on SDL's C++/CX files. Keep Vita3K and the other dependencies unchanged.
text = external.read_text()
assert text.count('add_subdirectory(sdl)') == 1
text = text.replace('add_subdirectory(sdl)', '''if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
  set(_vita3k_saved_cxx_standard ${CMAKE_CXX_STANDARD})
  set(CMAKE_CXX_STANDARD 17)
endif()
add_subdirectory(sdl)
if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
  set(CMAKE_CXX_STANDARD ${_vita3k_saved_cxx_standard})
  unset(_vita3k_saved_cxx_standard)
endif()''')
external.write_text(text)
print('Isolated SDL C++/CX targets at C++17 without downgrading the emulator core.')

# std::filesystem has native()/wstring(), but no Boost generic_path() member.
# All these consumers want a host path; Windows file APIs accept native paths.
for relative in [
    'io/include/io/filesystem.h', 'io/include/io/util.h', 'io/src/io.cpp',
    'util/src/fs_utils.cpp', 'util/src/logging.cpp', 'interface.cpp',
]:
    path = source / 'vita3k' / relative
    text = path.read_text()
    assert '.generic_path()' in text, relative
    path.write_text(text.replace('.generic_path()', ''))

# The libretro host owns presentation; Xbox never creates a Win32 HWND surface.
screen = source / 'vita3k/renderer/src/vulkan/screen_renderer.cpp'
text = screen.read_text()
old = '#ifdef _WIN32\n        vk::Win32SurfaceCreateInfoKHR create_info{};'
assert old in text
screen.write_text(text.replace(old, '#if defined(_WIN32) && !defined(VITA3K_UWP)\n        vk::Win32SurfaceCreateInfoKHR create_info{};', 1))

# Xbox has no desktop console to rename. Retain file/debugger/core callbacks.
logging = source / 'vita3k/util/src/logging.cpp'
text = logging.read_text()
old = '#ifdef _WIN32\n    // set console codepage to UTF-8'
assert old in text
logging.write_text(text.replace(old, '#if defined(_WIN32) && !defined(VITA3K_UWP)\n    // set console codepage to UTF-8', 1))

# Sparse-file hints are an optional storage optimization. Without desktop
# volume APIs, conservatively report no holes rather than assuming sparsity.
# Keep decryption, archive reads and the ordinary-file cache path intact.
lazy = source / 'vita3k/libretro/libretro_lazy_pkg.cpp'
text = lazy.read_text()
old = 'void mark_sparse(const fs::path &path) {\n#ifdef _WIN32'
assert old in text
text = text.replace(old, 'void mark_sparse(const fs::path &path) {\n#if defined(VITA3K_UWP)\n    (void)path;\n#elif defined(_WIN32)', 1)
old = 'static std::string fs_signature(const fs::path &dir) {\n#ifdef _WIN32'
assert old in text
text = text.replace(old, 'static std::string fs_signature(const fs::path &dir) {\n#if defined(VITA3K_UWP)\n    (void)dir;\n    return {};\n#elif defined(_WIN32)', 1)
old = 'static bool probe_holes(const fs::path &dir) {\n'
assert old in text
text = text.replace(old, old + '#if defined(VITA3K_UWP)\n    (void)dir;\n    return false;\n#else\n', 1)
old = '    return holes;\n}\n\nbool holes_supported'
assert old in text
text = text.replace(old, '    return holes;\n#endif\n}\n\nbool holes_supported', 1)
lazy.write_text(text)
print('Adapted native filesystem paths and excluded desktop window/console/sparse-volume helpers on Xbox.')

# Winsock 1's wsock32.dll is a desktop dependency. Keep guest networking on
# Winsock 2 and IP Helper rather than letting the linker resolve it via wsock32.
text = external.read_text()
old = 'target_link_libraries(winsock INTERFACE WSOCK32 WS2_32 IPHLPAPI)'
assert text.count(old) == 1
text = text.replace(old, '''if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    target_link_libraries(winsock INTERFACE WS2_32 IPHLPAPI)
  else()
    target_link_libraries(winsock INTERFACE WSOCK32 WS2_32 IPHLPAPI)
  endif()''')
external.write_text(text)

# The optional host crash logger used a desktop-only exception hook. Keep
# terminate/file/debugger logging; guest memory exception machinery is separate.
logging = source / 'vita3k/util/src/logging.cpp'
text = logging.read_text()
old = '#ifdef _WIN32\nstatic LONG WINAPI exception_handler'
assert old in text
logging.write_text(text.replace(old, '#if defined(_WIN32) && !defined(VITA3K_UWP)\nstatic LONG WINAPI exception_handler', 1))

# GetAdaptersInfo is hidden by the UWP SDK. Its supported successor returns
# real assigned IPv4 addresses and on-link prefix lengths for the guest.
network = source / 'vita3k/util/src/net_utils.cpp'
text = network.read_text()
old = '#include <winsock2.h>'
assert old in text
text = text.replace(old, old + '\n#include <ws2tcpip.h>', 1)
old = '#ifdef _WIN32\n    PIP_ADAPTER_INFO pAdapterInfo;'
assert old in text
text = text.replace(old, '''#if defined(VITA3K_UWP)
    ULONG bytes = 15 * 1024;
    std::vector<IP_ADAPTER_ADDRESSES> adapters;
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (unsigned attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        adapters.resize((bytes + sizeof(IP_ADAPTER_ADDRESSES) - 1) / sizeof(IP_ADAPTER_ADDRESSES));
        result = GetAdaptersAddresses(AF_INET,
            GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            nullptr, adapters.data(), &bytes);
    }
    if (result != NO_ERROR) {
        LOG_WARN("GetAdaptersAddresses failed with error: {}", result);
        return ret_addrs();
    }
    for (auto *adapter = adapters.data(); adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        for (auto *unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET)
                continue;
            const auto *address = reinterpret_cast<const sockaddr_in *>(unicast->Address.lpSockaddr);
            const unsigned prefix = unicast->OnLinkPrefixLength;
            if (prefix > 32) continue;
            in_addr mask{};
            mask.s_addr = htonl(prefix == 0 ? 0u : (0xffffffffu << (32 - prefix)));
            char ip[INET_ADDRSTRLEN] = {}, netmask[INET_ADDRSTRLEN] = {};
            if (!inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip)) ||
                !inet_ntop(AF_INET, &mask, netmask, sizeof(netmask))) continue;
            out_addrs.push_back({ adapter->AdapterName ? adapter->AdapterName : "network", ip, netmask });
        }
    }
#elif defined(_WIN32)
    PIP_ADAPTER_INFO pAdapterInfo;''', 1)
network.write_text(text)

# The pinned spdlog predates fmt 12's deprecated implicit format-string
# conversion. Use its explicit accessor instead of suppressing compiler errors.
logger = source / 'external/spdlog/include/spdlog/logger.h'
text = logger.read_text()
old = '        log_(loc, lvl, details::to_string_view(fmt), std::forward<Args>(args)...);'
assert text.count(old) == 2
new = '''#if !defined(SPDLOG_USE_STD_FORMAT) && FMT_VERSION >= 120000
        log_(loc, lvl, fmt.get(), std::forward<Args>(args)...);
#else
''' + old + '\n#endif'
text = text.replace(old, new, 1)
logger.write_text(text)
print('Fixed UWP utility exception logging, network enumeration and fmt 12 compatibility.')
