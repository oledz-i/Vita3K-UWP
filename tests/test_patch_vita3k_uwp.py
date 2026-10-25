#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression: UWP filesystem adapter removes both CMake Boost checks."""
from __future__ import annotations
import json
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[1]
script = repo / "scripts" / "patch-vita3k-uwp.py"
with tempfile.TemporaryDirectory(prefix="vita3k-uwp-fs-") as tmp:
    root = pathlib.Path(tmp)
    (root / "vita3k" / "util" / "include" / "util").mkdir(parents=True)
    (root / "vita3k" / "libretro").mkdir(parents=True)
    (root / "CMakeLists.txt").write_text("project(Vita3K)\nget_boost()\nadd_subdirectory(vita3k)\n")
    (root / "vita3k" / "CMakeLists.txt").write_text(
        "set(CMAKE_CXX_STANDARD 23)\nget_boost()\ninclude_directories(${Boost_INCLUDE_DIRS})\n"
    )
    (root / "vcpkg.json").write_text(json.dumps({
        "name": "vita3k", "version-string": "0",
        "dependencies": ["boost-filesystem", "boost-icl", "boost-program-options",
                          "boost-system", "boost-variant", "curl", "openssl", "zlib"],
    }))
    fs = root / "vita3k" / "util" / "include" / "util" / "fs.h"
    fs.write_text(
        "#include <boost/filesystem.hpp>\n"
        "#include <boost/filesystem/fstream.hpp>\n"
        "namespace fs = boost::filesystem;\n"
        "template<> struct fmt::formatter<boost::filesystem::path> : ostream_formatter {};\n"
    )
    for name in ["libretro_vita3k", "libretro_game_loader", "libretro_installer"]:
        (root / "vita3k" / "libretro" / f"{name}.cpp").write_text(
            'void check() { boost::system::error_code ec; fs::exists("x", ec); }\n'
        )
    subprocess.run([sys.executable, "-m", "py_compile", str(script)], check=True)
    subprocess.run([sys.executable, str(script), str(root)], check=True)
    top = (root / "CMakeLists.txt").read_text()
    nested = (root / "vita3k" / "CMakeLists.txt").read_text()
    for data in (top, nested):
        assert 'if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")' in data, data
        assert "else()\n  get_boost()\nendif()" in data, data
        assert "get_boost()" not in data.split("else()")[0], data
    assert "Boost_INCLUDE_DIRS" in nested
    assert "VITA3K_UWP" in top
    assert "namespace fs {" in fs.read_text()
    assert "fmt::formatter<fs::path>" in fs.read_text()
    manifest = json.loads((root / "vcpkg.json").read_text())
    assert manifest["dependencies"] == ["curl", "openssl", "zlib"]
    for name in ["libretro_vita3k", "libretro_game_loader", "libretro_installer"]:
        data = (root / "vita3k" / "libretro" / f"{name}.cpp").read_text()
        assert "std::error_code" in data and "boost::system::error_code" not in data
    print("UWP source filesystem patch: PASS (both CMake checks, libretro installer)")
