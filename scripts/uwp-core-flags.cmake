# Platform compiler settings, following OnyxEMU tools/uwp-flags.cmake's
# project-include approach. SDL alone uses C++17; the Vita core retains C++23.
if(MSVC AND CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    add_compile_options(/FIwinapifamily.h)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS
                           _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()
