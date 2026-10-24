// SPDX-License-Identifier: GPL-2.0-or-later
#include "VitaLibretroHost.h"
#include <algorithm>
#include <string>

VitaLibretroHost* VitaLibretroHost::active_ = nullptr;

namespace {
std::wstring FromUtf8(const char* input) {
    if (input == nullptr) return L"(unknown)";
    const int size = MultiByteToWideChar(CP_UTF8, 0, input, -1, nullptr, 0);
    if (size <= 1 || size > 2048) return L"(unavailable)";
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
    if (initialized_ && deinit_) deinit_();
    initialized_ = false;
    init_ = nullptr;
    deinit_ = nullptr;
    unsupportedVideo_ = false;
    if (active_ == this) active_ = nullptr;
    if (module_) {
        FreeLibrary(module_);
        module_ = nullptr;
    }
}

bool VitaLibretroHost::Probe(std::wstring& detail) {
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
    if (!api || !info || !environment || !video || !audio ||
        !audioBatch || !poll || !input || !init_ || !deinit_) {
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
    detail = L"Authentic Vita3K libretro ABI and exports validated (" + coreVersion_ +
        L"). Actual emulated CPU and GPU execution not yet tested.";
    return true;
}

bool VitaLibretroHost::Initialize(const std::string& systemFolder, std::wstring& detail) {
    if (!Probe(detail)) return false;
    if (initialized_) {
        detail = L"Real Vita3K libretro retro_init already executed.";
        return true;
    }
    systemDir_ = systemFolder;
    unsupportedVideo_ = false;
    if (!init_) return false;
    init_();
    initialized_ = true;
    detail = L"Real Vita3K libretro retro_init returned (" + coreVersion_ +
        L"). This is core initialization only, not Vita game boot.";
    if (unsupportedVideo_) {
        detail += L" Hardware Vulkan was requested but the UWP frontend cannot supply it.";
    }
    return true;
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
    case 14: // SET_HW_RENDER (OpenGL/Vulkan context negotiation)
        host->unsupportedVideo_ = true;
        return false;
    case 17: // GET_VARIABLE_UPDATE
        *static_cast<bool*>(data) = false;
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
    const void*, unsigned, unsigned, std::size_t) {
    // Hardware video frames require Vulkan and cannot be consumed by our
    // D3D12 BGRA test surface without a real Vulkan/D3D12 interop layer.
}
void __cdecl VitaLibretroHost::Audio(std::int16_t, std::int16_t) {}
std::size_t __cdecl VitaLibretroHost::AudioBatch(
    const std::int16_t*, std::size_t frames) { return frames; }
void __cdecl VitaLibretroHost::PollInput() {}
std::int16_t __cdecl VitaLibretroHost::Input(
    unsigned, unsigned, unsigned, unsigned) { return 0; }
