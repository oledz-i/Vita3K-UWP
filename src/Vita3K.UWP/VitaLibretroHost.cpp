// SPDX-License-Identifier: GPL-2.0-or-later
#include "VitaLibretroHost.h"
#include "CrashLog.h"
#include "UwpFileRedirect.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <share.h>
#include <string>
#include <chrono>
#pragma comment(lib, "xinputuap.lib")

VitaLibretroHost* VitaLibretroHost::active_ = nullptr;

namespace {
std::wstring FromUtf8(const char* input) {
    if (input == nullptr) return L"(unknown)";
    const int size = MultiByteToWideChar(CP_UTF8, 0, input, -1, nullptr, 0);
    if (size <= 1 || size > 16384) return L"(unavailable)";
    std::wstring output(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, input, -1, &output[0], size);
    output.pop_back();
    return output;
}
// A hardware fault inside the core (an access violation, say) would end the
// app with nothing in the log. These run a core call under SEH and report
// the exception code and where in which module it happened. C++ exceptions
// are left to their own handlers. The __try sits in functions without C++
// objects, as MSVC requires.
struct CoreFault {
    DWORD code = 0;
    const void* address = nullptr;
};
int CoreFaultFilter(EXCEPTION_POINTERS* info, CoreFault* fault) {
    if (info->ExceptionRecord->ExceptionCode == 0xE06D7363u) return EXCEPTION_CONTINUE_SEARCH;
    fault->code = info->ExceptionRecord->ExceptionCode;
    fault->address = info->ExceptionRecord->ExceptionAddress;
    return EXCEPTION_EXECUTE_HANDLER;
}
bool GuardedLoad(bool(__cdecl* load)(const void*), const void* game, bool* loaded, CoreFault* fault) {
    __try {
        *loaded = load(game);
        return true;
    } __except (CoreFaultFilter(GetExceptionInformation(), fault)) {
        return false;
    }
}
bool GuardedRun(void(__cdecl* run)(), CoreFault* fault) {
    __try {
        run();
        return true;
    } __except (CoreFaultFilter(GetExceptionInformation(), fault)) {
        return false;
    }
}
std::string DescribeFault(const char* where, const CoreFault& fault, HMODULE core) {
    MEMORY_BASIC_INFORMATION info{};
    const void* base = nullptr;
    if (fault.address && VirtualQuery(fault.address, &info, sizeof(info))) base = info.AllocationBase;
    const auto offset = static_cast<unsigned long long>(
        reinterpret_cast<const char*>(fault.address) - reinterpret_cast<const char*>(base));
    char text[256] = {};
    std::snprintf(text, sizeof(text), "%s crashed: exception 0x%08lX at %p (%s+0x%llX)\n", where, fault.code, fault.address,
        base == core ? "vita3k_libretro.dll" : base ? "another module" : "unmapped code", offset);
    return text;
}
template<typename T>
T Entry(HMODULE module, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}
}

VitaLibretroHost::~VitaLibretroHost() {
    Shutdown();
    if (logFile_) std::fclose(logFile_);
}

void VitaLibretroHost::LogLine(const std::string& line) {
    if (active_ == this) {
        Log(1, "%s\n", line.c_str());
        return;
    }
    // No core running yet (app start): straight to the file, or the line is lost.
    std::lock_guard<std::mutex> guard(logsMutex_);
    if (runtimeLogPath_.empty()) return;
    if (FILE* file = _wfsopen(runtimeLogPath_.c_str(), L"ab", _SH_DENYNO)) {
        std::fwrite(line.data(), 1, line.size(), file);
        std::fputc('\n', file);
        std::fclose(file);
    }
}

// The loading screen's stages, recognised in what the core logs while it
// installs and starts a game. Runs with logsMutex_ held.
void VitaLibretroHost::ObserveLog(const char* message) {
    if (!progressSink_ || !message) return;
    const std::string text = message;
    auto has = [&text](const char* needle) { return text.find(needle) != std::string::npos; };
    if (has("=== Checking game installation")) progressSink_("Checking the game", -1);
    else if (has("Encrypted game folder: decrypting") || has("Detected NoNpDrm encrypted content"))
        progressSink_("Installing the game: decrypting", 0);
    else if (has("PFS decryption: ")) {
        const auto at = text.find("PFS decryption: ") + 16;
        int percent = -1;
        try { percent = std::stoi(text.substr(at)); } catch (...) {}
        progressSink_("Installing the game: decrypting", percent);
    }
    else if (has("PFS decryption succeeded")) progressSink_("Installing the game: finishing", 100);
    else if (has("Installing '")) progressSink_("Installing the game", -1);
    else if (has("Not enough free space")) progressSink_("Not enough free space to install this game", -1);
    else if (has("retro_load_game.install")) progressSink_("Loading the game", -1);
    else if (has("context_reset_vulkan.enter")) progressSink_("Preparing graphics", -1);
    else if (has("context_reset_finalize.before_libretro_run_app")) progressSink_("Starting the game", -1);
    else if (has("First core video frame presented")) progressSink_("Waiting for the game's first picture", -1);
    else if (has("Render thread stopped"))
        progressSink_("!The console's graphics driver stopped, so this game can't draw. Close Vita3K completely and open it again, then save the log from Settings.", -1);
}

void VitaLibretroHost::Shutdown() {
    std::lock_guard<std::recursive_mutex> lifecycle(lifecycleMutex_);
    UnloadGame();
    if (initialized_ && deinit_) deinit_();
    initialized_ = false;
    audio_.Stop();
    init_ = nullptr;
    deinit_ = nullptr;
    loadGame_ = nullptr;
    unloadGame_ = nullptr;
    runFrame_ = nullptr;
    unsupportedVideo_ = false;
    if (active_ == this) active_ = nullptr;
    if (module_) {
        vita3k_uwp::DetachCrashLogChain(); // the core's filter goes with its DLL
        FreeLibrary(module_);
        module_ = nullptr;
    }
}

bool VitaLibretroHost::Probe(std::wstring& detail) {
    std::lock_guard<std::recursive_mutex> lifecycle(lifecycleMutex_);
    if (module_) {
        detail = L"A packaged Vita3K libretro core is loaded: " + coreVersion_;
        return true;
    }
    // LoadPackagedLibrary deliberately cannot load untrusted DLL files from
    // arbitrary user-selected game folders; use only signed APPX components.
    module_ = LoadPackagedLibrary(L"vita3k_libretro.dll", 0);
    if (!module_) {
        detail = L"No UWP-compatible Vita3K libretro core is packaged. "
            L"Windows loader error: " + std::to_wstring(GetLastError()) +
            L". A desktop Windows core DLL is not a tested substitute.";
        return false;
    }
    // Before the core touches a file: let it reach USB drives and other
    // storage the user granted, not only the app's own folders.
    redirectReport_ = vita3k_uwp::RedirectCoreFileAccess(module_);

    const auto api = Entry<ApiVersion>(module_, "retro_api_version");
    const auto info = Entry<GetSystemInfo>(module_, "retro_get_system_info");
    const auto environment = Entry<SetEnvironment>(module_, "retro_set_environment");
    const auto video = Entry<SetVideoRefresh>(module_, "retro_set_video_refresh");
    const auto audio = Entry<SetAudioSample>(module_, "retro_set_audio_sample");
    const auto audioBatch = Entry<SetAudioBatch>(module_, "retro_set_audio_sample_batch");
    const auto poll = Entry<SetInputPoll>(module_, "retro_set_input_poll");
    const auto input = Entry<SetInputState>(module_, "retro_set_input_state");
    init_ = Entry<Init>(module_, "retro_init");
    deinit_ = Entry<Deinit>(module_, "retro_deinit");
    loadGame_ = Entry<LoadGameFunction>(module_, "retro_load_game");
    unloadGame_ = Entry<UnloadGameFunction>(module_, "retro_unload_game");
    runFrame_ = Entry<RunFunction>(module_, "retro_run");
    if (!api || !info || !environment || !video || !audio ||
        !audioBatch || !poll || !input || !init_ || !deinit_ ||
        !loadGame_ || !unloadGame_ || !runFrame_) {
        detail = L"Core DLL is present but missing required libretro exports.";
        Shutdown();
        return false;
    }
    if (api() != 1u) {
        detail = L"Unexpected libretro API version: " + std::to_wstring(api());
        Shutdown();
        return false;
    }
    RetroSystemInfo system{};
    info(&system);
    const std::wstring name = FromUtf8(system.libraryName);
    const std::wstring version = FromUtf8(system.libraryVersion);
    if (name.find(L"Vita3K") == std::wstring::npos &&
        name.find(L"vita3k") == std::wstring::npos) {
        detail = L"The bundled libretro DLL does not identify itself as Vita3K.";
        Shutdown();
        return false;
    }
    coreVersion_ = name + L" " + version;
    active_ = this;
    environment(&Environment);
    video(&Video);
    audio(&Audio);
    audioBatch(&AudioBatch);
    poll(&PollInput);
    input(&Input);
    detail = L"Vita3K core API detected (" + coreVersion_ +
        L"). Actual emulated CPU and GPU execution not yet tested.";
    return true;
}

bool VitaLibretroHost::Initialize(const std::string& systemFolder, std::wstring& detail) {
    std::lock_guard<std::recursive_mutex> lifecycle(lifecycleMutex_);
    if (initialized_ && systemFolder == systemDir_) {
        detail = L"Vita3K core is already initialized.";
        return true;
    }
    if (initialized_) {
        // The Vita3K data folder moved: start the core again on the new one.
        Shutdown();
    }
    systemDir_ = systemFolder.c_str();
    if (runtimeLogPath_.empty()) {
        const auto parent = systemDir_.find_last_of("/\\");
        if (parent != std::string::npos)
            runtimeLogPath_ = FromUtf8((systemDir_.substr(0, parent + 1) + "vita3k-runtime.log").c_str());
    }
    if (!Probe(detail)) return false;
    unsupportedVideo_ = false;
    if (!init_) return false;
    if (!audio_.Start()) {
        Log(2, "Xbox audio output is unavailable; continuing without audio.\n");
    }
    Log(1, "Initializing packaged Vita3K core. Vita3K data: %s\n", systemDir_.c_str());
    Log(1, "External storage access (redirected file imports): %s\n", redirectReport_.c_str());
    init_();
    initialized_ = true;
    detail = L"Vita3K core initialized (" + coreVersion_ +
        L"). This is core initialization only, not Vita game boot.";
    if (unsupportedVideo_) {
        detail += L" Hardware Vulkan was requested but the UWP frontend cannot supply it.";
    }
    return true;
}


bool VitaLibretroHost::PupInfo(const std::string& pupUtf8, std::uint32_t& version, std::uint32_t& partitions) {
    std::lock_guard<std::recursive_mutex> lifecycle(lifecycleMutex_);
    version = 0;
    partitions = 0;
    std::wstring ignored;
    if (!Probe(ignored)) return false;
    using InfoFn = int(__cdecl*)(const char*, std::uint32_t*, std::uint32_t*);
    const auto info = Entry<InfoFn>(module_, "vita3k_uwp_pup_info");
    return info && info(pupUtf8.c_str(), &version, &partitions) != 0;
}
bool VitaLibretroHost::InstallFirmware(const std::string& systemFolder, const std::string& pupUtf8,
                                       std::function<void(std::uint32_t)> progress, std::string& version,
                                       std::wstring& detail) {
    std::lock_guard<std::recursive_mutex> lifecycle(lifecycleMutex_);
    if (gameLoaded_) {
        detail = L"Stop the game before installing firmware.";
        return false;
    }
    if (!Initialize(systemFolder, detail)) return false;
    using InstallFn = int(__cdecl*)(const char*, const char*, void(__cdecl*)(std::uint32_t, void*), void*, char*, std::uint32_t);
    const auto install = Entry<InstallFn>(module_, "vita3k_uwp_install_pup");
    if (!install) {
        detail = L"This Vita3K core cannot install firmware on request.";
        return false;
    }
    const std::string pref = systemFolder + "\\vita3k";
    char reported[64] = {};
    auto relay = [](std::uint32_t value, void* user) {
        auto* callback = static_cast<std::function<void(std::uint32_t)>*>(user);
        if (callback && *callback) (*callback)(value);
    };
    const bool ok = install(pref.c_str(), pupUtf8.c_str(), relay, &progress, reported, sizeof(reported)) != 0;
    version = reported;
    detail = ok ? L"Installed." : L"The firmware package could not be installed. " + RecentLog();
    return ok;
}
std::wstring VitaLibretroHost::RecentLog() const {
    std::lock_guard<std::mutex> guard(logsMutex_);
    return FromUtf8(latestCoreLogs_.c_str());
}
void VitaLibretroHost::UnloadGame() {
    std::lock_guard<std::recursive_mutex> lifecycle(lifecycleMutex_);
    StopRunning();
    // retro_unload_game stops the guest/render threads and frees the core's
    // Vulkan allocations. Its shared device must remain alive until it returns.
    if (gameLoaded_ && unloadGame_) unloadGame_();
    vulkan_.Shutdown();
    gameLoaded_ = false;
    sampleBuffer_.clear();
    framesExecuted_ = 0;
    framePresented_ = false;
}
bool VitaLibretroHost::LoadGame(const std::string& readableContentPath, std::wstring& detail) {
    std::lock_guard<std::recursive_mutex> lifecycle(lifecycleMutex_);
    if (!initialized_ || !loadGame_) {
        detail = L"Vita3K must initialize before loading a game.";
        return false;
    }
    if (readableContentPath.empty()) {
        detail = L"No game path supplied.";
        return false;
    }
    UnloadGame();
    {
        std::lock_guard<std::mutex> guard(logsMutex_);
        latestCoreLogs_.clear();
    }
    RetroGameInfo game{};
    game.path = readableContentPath.c_str();
    // The file remains owned by the caller; Vita3K's loader opens it using
    // its own filesystem/VFS APIs. A UWP picker token alone is insufficient.
    game.data = nullptr;
    game.size = 0;
    game.meta = nullptr;
    Log(1, "Loading staged content: %s\n", game.path);
    {
        CoreFault fault;
        bool loaded = false;
        if (!GuardedLoad(reinterpret_cast<bool(__cdecl*)(const void*)>(loadGame_), &game, &loaded, &fault)) {
            Log(3, "%s", DescribeFault("retro_load_game", fault, module_).c_str());
            gameLoaded_ = false;
            detail = L"Vita3K crashed while loading the game. vita3k-runtime.log says where.";
            return false;
        }
        gameLoaded_ = loaded;
    }
    Log(1, "retro_load_game returned %s\n", gameLoaded_ ? "true" : "false");
    if (!gameLoaded_) {
        detail = L"Vita3K rejected this game file. ";
        if (unsupportedVideo_) {
            detail += L"Vulkan hardware context is not yet connected to Dozen. ";
        }
        detail += L"Core log: " + RecentLog();
        return false;
    }
    std::string vulkanError;
    VitaVulkanBridge::SetDriverLog([](int level, const char* message) {
        // Mesa: 0 error, 1 warning, 2 info, 3 debug
        Log(level == 0 ? 3 : level == 1 ? 2 : 1, "[driver] %s\n", message);
    });
    if (!vulkan_.Initialize(vulkanError)) {
        Log(3, "Vulkan initialization failed: %s\n", vulkanError.c_str());
        detail = L"Vulkan initialization failed: " + FromUtf8(vulkanError.c_str());
        UnloadGame();
        return false;
    }
    // The core put its guest fault filter in front: back on top, chaining to it.
    vita3k_uwp::ReassertCrashLog();
    detail = L"Vita3K retro_load_game returned true. Guest execution and "
             L"visible game frames must still be verified.";
    return true;
}
bool VitaLibretroHost::RunFrame(std::wstring& detail) {
    std::lock_guard<std::recursive_mutex> lifecycle(lifecycleMutex_);
    if (running_.load()) { detail = L"Core is already advancing on its execution thread."; return false; }
    if (!gameLoaded_ || !runFrame_) {
        detail = L"No Vita game is loaded.";
        return false;
    }
    runFrame_();
    ++framesExecuted_;
    detail = L"Vita3K retro_run returned; tick " +
        std::to_wstring(framesExecuted_) +
        L". Verify a Vita guest thread and real rendered frames before "
        L"calling this gameplay.";
    return true;
}
void VitaLibretroHost::StopRunning() {
    std::lock_guard<std::recursive_mutex> lifecycle(lifecycleMutex_);
    running_.store(false);
    if (runThread_.joinable()) runThread_.join();
}
bool VitaLibretroHost::StartRunning(std::wstring& detail) {
    std::lock_guard<std::recursive_mutex> lifecycle(lifecycleMutex_);
    if (running_.load()) { detail = L"Vita3K session is already running."; return true; }
    StopRunning();
    if (!gameLoaded_ || !runFrame_ || unsupportedVideo_) {
        detail = L"Cannot run: game loading and hardware context initialization must succeed first. " + RecentLog();
        return false;
    }
    paused_.store(false);
    lastButtons_ = 0;
    running_.store(true);
    try {
        runThread_ = std::thread([this] {
            // The pinned core reports 60 Hz. Pace frames without running the
            // guest on the XAML dispatcher or spinning to catch up after stalls.
            constexpr auto period = std::chrono::microseconds(16667);
            try {
                while (running_.load()) {
                    const auto start = std::chrono::steady_clock::now();
                    if (paused_.load()) {
                        // No retro_run, so no vblank: the game waits where it is.
                        std::this_thread::sleep_until(start + period);
                        continue;
                    }
                    CoreFault fault;
                    if (!GuardedRun(runFrame_, &fault)) {
                        Log(3, "%s", DescribeFault("retro_run", fault, module_).c_str());
                        Log(3, "Emulation stopped after %llu frames.\n", framesExecuted_);
                        running_.store(false);
                        break;
                    }
                    ++framesExecuted_;
                    std::this_thread::sleep_until(start + period);
                }
            } catch (...) {
                Log(3, "Core frame execution threw an exception; session stopped.\n");
                running_.store(false);
            }
        });
    } catch (...) {
        running_.store(false);
        detail = L"Could not create the core execution thread.";
        return false;
    }
    detail = L"Vita3K is running. Press View + Menu together for the quick menu.";
    return true;
}
void VitaLibretroHost::SetOption(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> guard(optionsMutex_);
    const char* stored = optionStrings_.insert(value).first->c_str();
    auto& slot = options_[key];
    if (slot == nullptr || std::strcmp(slot, stored) != 0) {
        slot = stored;
        optionsChanged_.store(true);
    }
}
void __cdecl VitaLibretroHost::Log(unsigned level, const char* format, ...) {
    auto* host = active_;
    if (!host || !format) return;
    // Debug lines (every shader instruction the recompiler translates, each
    // file a game opens) cost more to write than the work they describe.
    if (level == 0 && !host->verboseLog_) return;
    char message[2048] = {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    std::lock_guard<std::mutex> guard(host->logsMutex_);
    if (!host->runtimeLogPath_.empty()) {
        // Kept open: a game install logs thousands of lines, and opening the
        // file for each one cost more than the work being logged.
        if (!host->logFile_ || host->logFilePath_ != host->runtimeLogPath_) {
            if (host->logFile_) std::fclose(host->logFile_);
            host->logFile_ = nullptr;
            // Shared, so the Device Portal (or anything else) can read it while
            // the app runs; _wfopen_s would lock it.
            host->logFile_ = _wfsopen(host->runtimeLogPath_.c_str(), L"ab", _SH_DENYNO);
            host->logFilePath_ = host->runtimeLogPath_;
            if (host->logFile_) std::setvbuf(host->logFile_, nullptr, _IOFBF, 64 * 1024);
        }
        if (host->logFile_) {
            std::fwrite(message, 1, std::strlen(message), host->logFile_);
            // A flush per line was a write to the drive for every file a game
            // opened (hundreds a second in some games). Warnings and errors
            // still go out at once; the rest at least every quarter second.
            const unsigned long long now = GetTickCount64();
            if (level >= 2 || now - host->lastLogFlush_ >= 250) {
                std::fflush(host->logFile_);
                host->lastLogFlush_ = now;
            }
        }
    }
    host->ObserveLog(message);
    host->latestCoreLogs_.append(message);
    if (host->latestCoreLogs_.size() > 12000) {
        host->latestCoreLogs_.erase(0, host->latestCoreLogs_.size() - 12000);
        while (!host->latestCoreLogs_.empty() &&
            (static_cast<unsigned char>(host->latestCoreLogs_.front()) & 0xc0) == 0x80)
            host->latestCoreLogs_.erase(0, 1);
    }
}
bool __cdecl VitaLibretroHost::Environment(unsigned cmd, void* data) {
    auto* host = active_;
    if (!host || !data) return false;
    switch (cmd) {
    case 9:  // GET_SYSTEM_DIRECTORY
    case 30: // GET_CONTENT_DIRECTORY
    case 31: // GET_SAVE_DIRECTORY
        *static_cast<const char**>(data) = host->systemDir_.c_str();
        return true;
    case 10: // SET_PIXEL_FORMAT
        // RGB1555 (0), XRGB8888 (1), RGB565 (2) accepted for software video
        return *static_cast<const unsigned*>(data) <= 2;
    case 27: // GET_LOG_INTERFACE, receives a variadic logging callback
        *static_cast<RetroLogCallback*>(data) = {&VitaLibretroHost::Log};
        return true;
    case 56: // GET_PREFERRED_HW_RENDER
        *static_cast<unsigned*>(data) = 6; // Vulkan (Onyx Dozen backend)
        return true;
    case 14: // SET_HW_RENDER (OpenGL/Vulkan context negotiation)
        host->unsupportedVideo_ = !host->vulkan_.Accept(static_cast<retro_hw_render_callback*>(data));
        return !host->unsupportedVideo_;
    case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE:
        return host->vulkan_.Negotiate(static_cast<const retro_hw_render_context_negotiation_interface_vulkan*>(data));
    case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: {
        auto api = host->vulkan_.Interface();
        if (!api) return false;
        *static_cast<const retro_hw_render_interface_vulkan**>(data) = api;
        return true;
    }
    case 15: { // GET_VARIABLE
        auto* variable = static_cast<retro_variable*>(data);
        if (!variable->key) return false;
        std::lock_guard<std::mutex> guard(host->optionsMutex_);
        const auto found = host->options_.find(variable->key);
        if (found == host->options_.end()) { variable->value = nullptr; return false; }
        variable->value = found->second;
        return true;
    }
    case 70: { // SET_VARIABLE: the core changes one of its own options
        const auto* variable = static_cast<const retro_variable*>(data);
        if (!variable->key || !variable->value) return false;
        std::lock_guard<std::mutex> guard(host->optionsMutex_);
        host->options_[variable->key] = host->optionStrings_.insert(variable->value).first->c_str();
        return true;
    }
    case 17: // GET_VARIABLE_UPDATE
        *static_cast<bool*>(data) = host->optionsChanged_.exchange(false);
        return true;
    case 18: // SET_SUPPORT_NO_GAME
        return true;
    case 52: // GET_CORE_OPTIONS_VERSION
        *static_cast<unsigned*>(data) = 0;
        return true;
    default:
        // Return false for services we have not actually implemented.
        // Claiming success for Vulkan, VFS, system callbacks or graphics
        // context negotiation would cause misleading, unsafe execution.
        return false;
    }
}

void __cdecl VitaLibretroHost::Video(
    const void* data, unsigned width, unsigned height, std::size_t) {
    if (!active_ || data != RETRO_HW_FRAME_BUFFER_VALID) return;
    std::string error;
    if (active_->vulkan_.Present(width, height, error)) {
        if (!active_->framePresented_.exchange(true))
            Log(1, "First core video frame presented: %u x %u (%s).\n", width, height, active_->vulkan_.ReadbackInfo().c_str());
    } else if (!error.empty()) {
        // Once a frame fails the next ones usually fail the same way; keep the log readable.
        static std::atomic<unsigned> failures{0};
        const unsigned n = ++failures;
        if (n <= 5) Log(3, "Vulkan frame readback failed: %s\n", error.c_str());
        if (n == 5) Log(3, "Further frame readback errors are not logged.\n");
    }
}
void __cdecl VitaLibretroHost::Audio(std::int16_t left, std::int16_t right) {
    if (!active_) return;
    auto& buffer = active_->sampleBuffer_;
    buffer.push_back(left); buffer.push_back(right);
    if (buffer.size() >= 1024) {
        active_->audio_.Push(buffer.data(), buffer.size() / 2);
        buffer.clear();
    }
}
std::size_t __cdecl VitaLibretroHost::AudioBatch(
    const std::int16_t* samples, std::size_t frames) {
    return active_ ? active_->audio_.Push(samples, frames) : 0;
}
void __cdecl VitaLibretroHost::PollInput() {
    if (!active_) return;
    // Take one coherent controller snapshot for the emulated frame.
    active_->inputConnected_ = XInputGetState(0, &active_->inputState_) == ERROR_SUCCESS;
    if (!active_->inputConnected_) active_->inputState_ = {};
    const auto buttons = active_->inputState_.Gamepad.wButtons;
    constexpr WORD combo = XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START;
    const bool held = (buttons & combo) == combo;
    const bool wasHeld = (active_->lastButtons_ & combo) == combo;
    active_->lastButtons_ = buttons;
    if (held && !wasHeld) {
        // Pause and open the quick menu; quitting is one of its choices, so
        // a stray press no longer throws the game away.
        active_->paused_.store(true);
        std::lock_guard<std::mutex> guard(active_->menuMutex_);
        if (active_->menuSink_) active_->menuSink_();
        else active_->running_.store(false);
    }
}
std::int16_t __cdecl VitaLibretroHost::Input(
    unsigned port, unsigned device, unsigned index, unsigned id) {
    if (!active_ || !active_->inputConnected_ || port != 0) return 0;
    const auto& pad = active_->inputState_.Gamepad;
    if ((device & 0xff) == 1 && index == 0) {
        // libretro RetroPad IDs: B,Y,Select,Start,Up,Down,Left,Right,A,X,L,R,L2,R2,L3,R3.
        static const WORD buttons[] = {XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_X,
            XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_DPAD_UP,
            XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
            XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_Y, XINPUT_GAMEPAD_LEFT_SHOULDER,
            XINPUT_GAMEPAD_RIGHT_SHOULDER, 0, 0, XINPUT_GAMEPAD_LEFT_THUMB,
            XINPUT_GAMEPAD_RIGHT_THUMB};
        if (id == 12) return pad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        if (id == 13) return pad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        return id < 16 && (pad.wButtons & buttons[id]) ? 1 : 0;
    }
    if ((device & 0xff) == 5 && index < 2 && id < 2) {
        const short axis = index == 0 ? (id == 0 ? pad.sThumbLX : pad.sThumbLY)
                                      : (id == 0 ? pad.sThumbRX : pad.sThumbRY);
        const int deadzone = index == 0 ? XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE : XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
        if (axis > -deadzone && axis < deadzone) return 0;
        // libretro analog Y is positive down; saturate -(-32768).
        return id == 0 ? axis : static_cast<std::int16_t>(axis == -32768 ? 32767 : -axis);
    }
    return 0;
}
