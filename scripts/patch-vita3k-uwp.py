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
removed = {"boost-filesystem", "boost-icl", "boost-program-options",
           "boost-system", "boost-variant"}
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
    "  include_directories(\"\u0024{CMAKE_SOURCE_DIR}/external/boost\")\n"
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
