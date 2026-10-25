#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep desktop-only WinMM and CppCommon UWP patches reproducible."""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
patch = root / "scripts/patch-vita3k-uwp-deps.py"
subprocess.run([sys.executable, "-m", "py_compile", str(patch)], check=True)
with tempfile.TemporaryDirectory(prefix="vita3k-xbox-audio-") as directory:
    tmp = pathlib.Path(directory)
    sources = {
        "external/CMakeLists.txt":
            'add_subdirectory(cubeb EXCLUDE_FROM_ALL)\n',
        "vita3k/audio/CMakeLists.txt":
            "if(BUILD_LIBRETRO)\n"
            "    add_library(\n        audio\n        STATIC\n"
            "        src/audio.cpp\n        src/impl/cubeb_audio.cpp\n"
            "        src/impl/libretro_audio.cpp)\n",
        "vita3k/audio/src/audio.cpp":
            '#include <audio/impl/cubeb_audio.h>\n'
            '    } else if (adapter_name == "Cubeb") {\n'
            '        adapter = std::make_unique<CubebAudioAdapter>(*this);\n'
            '    } else {\n'
            '        LOG_ERROR("Unknown audio adapter {}", adapter_name);\n'
            '        return;\n'
            '    }\n#else\n',
        "external/CppCommon/source/environment.cpp":
            '#elif defined(_WIN32) || defined(_WIN64)\n'
            '    static NTSTATUS foo;\n',
    }
    for relative, source in sources.items():
        path = tmp / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(source)
    subprocess.run([sys.executable, str(patch), str(tmp)], check=True)
    external = (tmp / "external/CMakeLists.txt").read_text()
    assert 'add_library(cubeb INTERFACE)' in external
    audio_cmake = (tmp / "vita3k/audio/CMakeLists.txt").read_text()
    assert 'VITA3K_LIBRETRO_CUBEB_SOURCE' in audio_cmake
    assert 'CMAKE_SYSTEM_NAME STREQUAL "WindowsStore"' in audio_cmake
    audio_code = (tmp / "vita3k/audio/src/audio.cpp").read_text()
    assert '#ifndef VITA3K_UWP\n#include <audio/impl/cubeb_audio.h>\n#endif' in audio_code
    env = (tmp / "external/CppCommon/source/environment.cpp").read_text()
    assert 'return "Windows UWP";' in env
    print("Vita3K WindowsStore dependency patches: PASS (Cubeb, CppCommon, libretro)")
