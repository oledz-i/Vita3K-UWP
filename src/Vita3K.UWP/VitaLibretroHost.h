// SPDX-License-Identifier: GPL-2.0-or-later
// A genuine libretro C ABI host probe for a Windows Store-compatible Vita3K
// libretro core *if one is successfully built and packaged*. This does not
// provide or substitute the missing CPU, kernel, loader or GPU.
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>

class VitaLibretroHost final {
public:
    VitaLibretroHost() = default;
    ~VitaLibretroHost();
    VitaLibretroHost(const VitaLibretroHost&) = delete;
    VitaLibretroHost& operator=(const VitaLibretroHost&) = delete;

    // Load only a core DLL included and signed in our APPX package.
    bool Probe(std::wstring& detail);
    // Invoke real exported libretro setup / retro_init when available.
    bool Initialize(const std::string& systemFolder, std::wstring& detail);
    bool CoreLoaded() const { return module_ != nullptr; }
    bool Initialized() const { return initialized_; }
    bool RequestedUnsupportedGraphics() const { return unsupportedVideo_; }
    void Shutdown();

private:
    struct RetroSystemInfo {
        const char* libraryName;
        const char* libraryVersion;
        const char* validExtensions;
        bool needFullpath;
        bool blockExtract;
    };
    using RetroEnvironment = bool(__cdecl*)(unsigned, void*);
    using RetroVideoRefresh = void(__cdecl*)(const void*, unsigned, unsigned, std::size_t);
    using RetroAudioSample = void(__cdecl*)(std::int16_t, std::int16_t);
    using RetroAudioBatch = std::size_t(__cdecl*)(const std::int16_t*, std::size_t);
    using RetroInputPoll = void(__cdecl*)();
    using RetroInputState = std::int16_t(__cdecl*)(unsigned, unsigned, unsigned, unsigned);
    using SetEnvironment = void(__cdecl*)(RetroEnvironment);
    using SetVideoRefresh = void(__cdecl*)(RetroVideoRefresh);
    using SetAudioSample = void(__cdecl*)(RetroAudioSample);
    using SetAudioBatch = void(__cdecl*)(RetroAudioBatch);
    using SetInputPoll = void(__cdecl*)(RetroInputPoll);
    using SetInputState = void(__cdecl*)(RetroInputState);
    using GetSystemInfo = void(__cdecl*)(RetroSystemInfo*);
    using Init = void(__cdecl*)();
    using Deinit = void(__cdecl*)();
    using ApiVersion = unsigned(__cdecl*)();

    static bool __cdecl Environment(unsigned command, void* value);
    static void __cdecl Video(const void* data, unsigned width, unsigned height, std::size_t pitch);
    static void __cdecl Audio(std::int16_t left, std::int16_t right);
    static std::size_t __cdecl AudioBatch(const std::int16_t* samples, std::size_t frames);
    static void __cdecl PollInput();
    static std::int16_t __cdecl Input(unsigned port, unsigned device, unsigned index, unsigned id);

    HMODULE module_ = nullptr;
    Init init_ = nullptr;
    Deinit deinit_ = nullptr;
    std::string systemDir_;
    bool initialized_ = false;
    bool unsupportedVideo_ = false;
    std::wstring coreVersion_;
    static VitaLibretroHost* active_;
};
