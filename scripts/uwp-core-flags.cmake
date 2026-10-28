# Platform compiler settings, following OnyxEMU tools/uwp-flags.cmake's
# project-include approach. SDL alone uses C++17; the Vita core retains C++23.
if(MSVC AND CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    add_compile_options(/FIwinapifamily.h)
    # Xbox Series X|S run Zen 2 cores: let the compiler use AVX2 (texture
    # conversion, hashing and audio mixing are vectorised with it). The JIT
    # already picks its instructions from the CPU at run time.
    add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/arch:AVX2>)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS
                           _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()
