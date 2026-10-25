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
