// SPDX-License-Identifier: GPL-2.0-or-later
#include "VulkanDznProbe.h"
#include <windows.h>
#include <cstdint>
#include <string>

namespace {
using VkInstance = void*;
using VkPhysicalDevice = void*;
using VkResult = std::int32_t;
struct VkApplicationInfo {
    std::uint32_t sType;
    const void* pNext;
    const char* pApplicationName;
    std::uint32_t applicationVersion;
    const char* pEngineName;
    std::uint32_t engineVersion;
    std::uint32_t apiVersion;
};
struct VkInstanceCreateInfo {
    std::uint32_t sType;
    const void* pNext;
    std::uint32_t flags;
    const VkApplicationInfo* pApplicationInfo;
    std::uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    std::uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};
using GetProc = void* (__stdcall*)(VkInstance, const char*);
using CreateInstance = VkResult (__stdcall*)(
    const VkInstanceCreateInfo*, const void*, VkInstance*);
using DestroyInstance = void (__stdcall*)(VkInstance, const void*);
using EnumeratePhysicalDevices = VkResult (__stdcall*)(
    VkInstance, std::uint32_t*, VkPhysicalDevice*);
using EnumerateVersion = VkResult (__stdcall*)(std::uint32_t*);

std::wstring Err(DWORD error) { return std::to_wstring(error); }
}
std::wstring ProbePackagedVulkan() {
    // A desktop-installed Vulkan runtime is not available to a standalone
    // Xbox UWP process. If we port Mesa Dozen it must be packaged with its
    // proper Vulkan loader/ICD and compiled for the Windows Store API family.
    HMODULE loader = LoadPackagedLibrary(L"vulkan-1.dll", 0);
    if (!loader) return L"Vulkan loader NOT packaged (Win32 " +
        Err(GetLastError()) +
        L"). Native D3D12 working does not imply a working Dozen Vulkan driver.";

    auto getProc = reinterpret_cast<GetProc>(GetProcAddress(loader, "vkGetInstanceProcAddr"));
    auto getVersion = reinterpret_cast<EnumerateVersion>(
        GetProcAddress(loader, "vkEnumerateInstanceVersion"));
    if (!getProc) {
        FreeLibrary(loader);
        return L"Packaged vulkan-1.dll lacks vkGetInstanceProcAddr.";
    }
    std::uint32_t version = 1u << 22;
    if (getVersion && getVersion(&version) != 0) version = 1u << 22;
    const std::uint32_t major = version >> 22;
    const std::uint32_t minor = (version >> 12) & 1023u;
    if (major < 1 || (major == 1 && minor < 1)) {
        FreeLibrary(loader);
        return L"Vulkan loader is older than required Vulkan 1.1 (reports " +
            std::to_wstring(major) + L"." + std::to_wstring(minor) + L").";
    }
    const auto create = reinterpret_cast<CreateInstance>(getProc(nullptr, "vkCreateInstance"));
    if (!create) {
        FreeLibrary(loader);
        return L"Vulkan 1.1 loader found, but vkCreateInstance is unavailable.";
    }
    VkApplicationInfo app = {};
    app.sType = 0; // VK_STRUCTURE_TYPE_APPLICATION_INFO
    app.pApplicationName = "Vita3K UWP feasibility";
    app.applicationVersion = 1;
    app.pEngineName = "Vita3K-UWP";
    app.engineVersion = 1;
    app.apiVersion = (1u << 22) | (1u << 12); // Vulkan 1.1.0
    VkInstanceCreateInfo info = {};
    info.sType = 1; // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    info.pApplicationInfo = &app;
    VkInstance instance = nullptr;
    const VkResult created = create(&info, nullptr, &instance);
    if (created != 0 || !instance) {
        FreeLibrary(loader);
        return L"Vulkan loader found, but vkCreateInstance failed (VkResult " +
            std::to_wstring(created) + L"). The Dozen ICD may be missing/incompatible.";
    }
    const auto enumerate = reinterpret_cast<EnumeratePhysicalDevices>(
        getProc(instance, "vkEnumeratePhysicalDevices"));
    const auto destroy = reinterpret_cast<DestroyInstance>(
        getProc(instance, "vkDestroyInstance"));
    std::uint32_t count = 0;
    const VkResult enumerated = enumerate
        ? enumerate(instance, &count, nullptr) : static_cast<VkResult>(-1);
    if (destroy) destroy(instance, nullptr);
    FreeLibrary(loader);
    if (enumerated != 0 || count == 0) {
        return L"Vulkan instance created but no Vulkan physical device enumerated "
            L"(result " + std::to_wstring(enumerated) +
            L", count " + std::to_wstring(count) + L").";
    }
    return L"Vulkan 1.1+ loader and " + std::to_wstring(count) +
        L" physical device(s) enumerated. This does NOT prove Dozen compatibility, "
        L"Vita3K shader requirements or swap-chain integration.";
}
