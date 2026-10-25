// SPDX-License-Identifier: GPL-2.0-or-later
// A genuine libretro C ABI host probe for a Windows Store-compatible Vita3K
// libretro core *if one is successfully built and packaged*. This does not
// provide or substitute the missing CPU, kernel, loader or GPU.
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <mutex>
#include <atomic>
#include <thread>
#include <Xinput.h>
#include "CoreAudio.h"

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
    bool GameLoaded() const { return gameLoaded_; }
    // Use a file staged into the UWP app sandbox or exposed by a working VFS.
    // This invokes Vita3K's retro_load_game, never a simulated boot screen.
    bool LoadGame(const std::string& readableContentPath, std::wstring& detail);
    // retro_run advances guest CPU and graphics only after LoadGame succeeds.
    bool RunFrame(std::wstring& detail);
    bool StartRunning(std::wstring& detail);
    void StopRunning();
    bool Running() const { return running_.load(); }
    void UnloadGame();
    std::wstring RecentLog() const;
    void Shutdown();

private:
    struct RetroGameInfo {
        const char* path;
        const void* data;
        std::size_t size;
        const char* meta;
    };
    struct RetroLogCallback {
        void (__cdecl* log)(unsigned level, const char* format, ...);
    };
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
    using LoadGameFunction = bool(__cdecl*)(const RetroGameInfo*);
    using UnloadGameFunction = void(__cdecl*)();
    using RunFunction = void(__cdecl*)();

    static bool __cdecl Environment(unsigned command, void* value);
    static void __cdecl Log(unsigned level, const char* format, ...);
    static void __cdecl Video(const void* data, unsigned width, unsigned height, std::size_t pitch);
    static void __cdecl Audio(std::int16_t left, std::int16_t right);
    static std::size_t __cdecl AudioBatch(const std::int16_t* samples, std::size_t frames);
    static void __cdecl PollInput();
    static std::int16_t __cdecl Input(unsigned port, unsigned device, unsigned index, unsigned id);

    std::atomic<bool> running_{false};
    std::thread runThread_;
    CoreAudio audio_;
    std::vector<std::int16_t> sampleBuffer_;
    XINPUT_STATE inputState_{};
    bool inputConnected_ = false;
    HMODULE module_ = nullptr;
    Init init_ = nullptr;
    Deinit deinit_ = nullptr;
    LoadGameFunction loadGame_ = nullptr;
    UnloadGameFunction unloadGame_ = nullptr;
    RunFunction runFrame_ = nullptr;
    std::string systemDir_;
    bool initialized_ = false;
    bool gameLoaded_ = false;
    unsigned long long framesExecuted_ = 0;
    mutable std::mutex logsMutex_;
    std::string latestCoreLogs_;
    bool unsupportedVideo_ = false;
    std::wstring coreVersion_;
    static VitaLibretroHost* active_;
};
