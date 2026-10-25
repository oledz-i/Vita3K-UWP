// SPDX-License-Identifier: GPL-2.0-or-later
#include "VitaLibretroHost.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
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
template<typename T>
T Entry(HMODULE module, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}
}

VitaLibretroHost::~VitaLibretroHost() {
    Shutdown();
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
    if (initialized_) {
        detail = L"Vita3K core is already initialized.";
        return true;
    }
    systemDir_ = systemFolder.c_str();
    // Keep frontend diagnostics outside the core's firmware directory so they
    // survive load failures. The caller passes LocalFolder/Vita3KSystem.
    const auto parent = systemDir_.find_last_of("/\\");
    if (parent != std::string::npos)
        runtimeLogPath_ = FromUtf8((systemDir_.substr(0, parent + 1) + "vita3k-runtime.log").c_str());
    if (!Probe(detail)) return false;
    unsupportedVideo_ = false;
    if (!init_) return false;
    if (!audio_.Start()) {
        Log(2, "Xbox audio output is unavailable; continuing without audio.\n");
    }
    Log(1, "Initializing packaged Vita3K core.\n");
    init_();
    initialized_ = true;
    detail = L"Vita3K core initialized (" + coreVersion_ +
        L"). This is core initialization only, not Vita game boot.";
    if (unsupportedVideo_) {
        detail += L" Hardware Vulkan was requested but the UWP frontend cannot supply it.";
    }
    return true;
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
    gameLoaded_ = loadGame_(&game);
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
    if (!vulkan_.Initialize(vulkanError)) {
        Log(3, "Vulkan initialization failed: %s\n", vulkanError.c_str());
        detail = L"Vulkan initialization failed: " + FromUtf8(vulkanError.c_str());
        UnloadGame();
        return false;
    }
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
    running_.store(true);
    try {
        runThread_ = std::thread([this] {
            // The pinned core reports 60 Hz. Pace frames without running the
            // guest on the XAML dispatcher or spinning to catch up after stalls.
            constexpr auto period = std::chrono::microseconds(16667);
            try {
                while (running_.load()) {
                    const auto start = std::chrono::steady_clock::now();
                    runFrame_();
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
    detail = L"Vita3K is running. Press View + Menu together to stop.";
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
void __cdecl VitaLibretroHost::Log(unsigned, const char* format, ...) {
    auto* host = active_;
    if (!host || !format) return;
    char message[2048] = {};
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    std::lock_guard<std::mutex> guard(host->logsMutex_);
    if (!host->runtimeLogPath_.empty()) {
        FILE* file = nullptr;
        if (_wfopen_s(&file, host->runtimeLogPath_.c_str(), L"ab") == 0 && file) {
            std::fwrite(message, 1, std::strlen(message), file);
            std::fclose(file);
        }
    }
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
            Log(1, "First core video frame presented: %u x %u.\n", width, height);
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
    if ((buttons & (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START)) ==
        (XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_START)) {
        active_->running_.store(false);
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
