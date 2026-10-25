// SPDX-License-Identifier: GPL-3.0-or-later
// Vita3K UWP Vulkan capability probe adapted from OnyxEMU's working Xbox
// VulkanHost::DozenDriver ICD bootstrap (oledz-i/OnyxEMU, GPL-3.0-or-later).
// On Xbox UWP there is no system vulkan-1.dll: Mesa's vulkan_dzn.dll must be
// loaded directly via the Vulkan Installable Client Driver (ICD) ABI.
// This probe does not render frames or claim Vita GXM integration.
#include "VulkanDznProbe.h"
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
using VkResult = std::int32_t;
using VkInstance = void*;
using VkPhysicalDevice = void*;
constexpr VkResult kVkSuccess = 0;
constexpr unsigned kVulkan11 = (1u << 22) | (1u << 12);

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
struct VkExtensionProperties {
    char extensionName[256];
    std::uint32_t specVersion;
};
struct VkExtent3D {
    std::uint32_t width, height, depth;
};
struct VkQueueFamilyProperties {
    std::uint32_t queueFlags;
    std::uint32_t queueCount;
    std::uint32_t timestampValidBits;
    VkExtent3D minImageTransferGranularity;
};

using GetProc = void* (__stdcall*)(VkInstance, const char*);
using NegotiateIcd = VkResult (__stdcall*)(std::uint32_t*);
using CreateInstance = VkResult (__stdcall*)(const VkInstanceCreateInfo*, const void*, VkInstance*);
using DestroyInstance = void (__stdcall*)(VkInstance, const void*);
using EnumeratePhysicalDevices = VkResult (__stdcall*)(VkInstance, std::uint32_t*, VkPhysicalDevice*);
using EnumerateDeviceExtensions = VkResult (__stdcall*)(VkPhysicalDevice, const char*, std::uint32_t*, VkExtensionProperties*);
using GetQueueFamilies = void (__stdcall*)(VkPhysicalDevice, std::uint32_t*, VkQueueFamilyProperties*);
using EnumerateInstanceVersion = VkResult (__stdcall*)(std::uint32_t*);

std::wstring VerifyVulkanInterface(HMODULE library, GetProc getProc,
    const std::wstring& kind, std::uint32_t icdInterface) {
    if (!getProc) return kind + L": vkGetInstanceProcAddr entry point missing.";
    const auto create = reinterpret_cast<CreateInstance>(
        getProc(nullptr, "vkCreateInstance"));
    if (!create) return kind + L": vkCreateInstance entry point missing.";

    VkApplicationInfo application{};
    application.sType = 0; // VK_STRUCTURE_TYPE_APPLICATION_INFO
    application.pApplicationName = "Vita3K UWP";
    application.applicationVersion = 1;
    application.pEngineName = "Onyx Mesa Dozen ICD probe";
    application.engineVersion = 1;
    application.apiVersion = kVulkan11;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = 1; // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
    createInfo.pApplicationInfo = &application;

    VkInstance instance = nullptr;
    const VkResult result = create(&createInfo, nullptr, &instance);
    if (result != kVkSuccess || !instance) {
        return kind + L": Vulkan 1.1 vkCreateInstance failed (VkResult " +
            std::to_wstring(result) + L"). Driver may not support UWP.";
    }

    const auto enumerate = reinterpret_cast<EnumeratePhysicalDevices>(
        getProc(instance, "vkEnumeratePhysicalDevices"));
    const auto destroy = reinterpret_cast<DestroyInstance>(
        getProc(instance, "vkDestroyInstance"));
    if (!enumerate || !destroy) {
        if (destroy) destroy(instance, nullptr);
        return kind + L": Vulkan instance created but GPU enumeration/teardown exports absent.";
    }

    std::uint32_t count = 0;
    const VkResult countResult = enumerate(instance, &count, nullptr);
    if (countResult != kVkSuccess || !count || count > 128) {
        destroy(instance, nullptr);
        return kind + L": instance created, but GPU discovery returned " +
            std::to_wstring(countResult) + L" with " +
            std::to_wstring(count) + L" device(s).";
    }
    std::vector<VkPhysicalDevice> devices(count);
    const VkResult listResult = enumerate(instance, &count, devices.data());
    if (listResult != kVkSuccess || !count) {
        destroy(instance, nullptr);
        return kind + L": Vulkan adapter enumeration failed (VkResult " +
            std::to_wstring(listResult) + L").";
    }

    std::wstring message = kind + L": Vulkan 1.1 instance created with " +
        std::to_wstring(count) + L" adapter(s)";
    if (icdInterface) message += L"; ICD interface v" + std::to_wstring(icdInterface);
    const auto extensions = reinterpret_cast<EnumerateDeviceExtensions>(
        getProc(instance, "vkEnumerateDeviceExtensionProperties"));
    const auto queues = reinterpret_cast<GetQueueFamilies>(
        getProc(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    if (extensions && queues) {
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t familyCount = 0;
            queues(devices[i], &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(
                std::min(familyCount, 64u));
            if (!families.empty()) {
                familyCount = static_cast<std::uint32_t>(families.size());
                queues(devices[i], &familyCount, families.data());
            }
            const bool graphics = std::any_of(families.begin(), families.end(),
                [](const VkQueueFamilyProperties& family) {
                    return family.queueCount != 0 && (family.queueFlags & 1u) != 0;
                });

            std::uint32_t extCount = 0;
            const VkResult extResult = extensions(devices[i], nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> extensionsList;
            if (extResult == kVkSuccess && extCount < 4096u) {
                extensionsList.resize(extCount);
                if (extCount) extensions(devices[i], nullptr, &extCount, extensionsList.data());
            }
            auto hasExtension = [&extensionsList](const char* name) {
                return std::any_of(extensionsList.begin(), extensionsList.end(),
                    [name](const VkExtensionProperties& item) {
                        return std::strcmp(item.extensionName, name) == 0;
                    });
            };
            message += L"; GPU " + std::to_wstring(i) + L" graphics=" +
                (graphics ? std::wstring(L"yes") : std::wstring(L"no"));
            message += L" external_memory_win32=" +
                (hasExtension("VK_KHR_external_memory_win32") ? std::wstring(L"yes") : std::wstring(L"no"));
            message += L" external_semaphore_win32=" +
                (hasExtension("VK_KHR_external_semaphore_win32") ? std::wstring(L"yes") : std::wstring(L"no"));
        }
    }
    destroy(instance, nullptr);
    (void)library;
    return message + L". Vulkan device detection is NOT Vita3K GXM rendering or gameplay.";
}
} // namespace

std::wstring ProbePackagedVulkan() {
    // Directly use the real Mesa ICD path from OnyxEMU rather than expecting
    // the desktop Vulkan loader to discover a driver inside Xbox AppContainer.
    HMODULE dozen = LoadPackagedLibrary(L"vulkan_dzn.dll", 0);
    if (dozen) {
        auto getProc = reinterpret_cast<GetProc>(
            GetProcAddress(dozen, "vk_icdGetInstanceProcAddr"));
        if (!getProc) {
            FreeLibrary(dozen);
            return L"Packaged Onyx Mesa Dozen has no vk_icdGetInstanceProcAddr export.";
        }
        unsigned version = 5; // OnyxEMU negotiates ICD v5; driver may lower.
        const auto negotiate = reinterpret_cast<NegotiateIcd>(
            GetProcAddress(dozen, "vk_icdNegotiateLoaderICDInterfaceVersion"));
        if (negotiate && negotiate(&version) != kVkSuccess) {
            FreeLibrary(dozen);
            return L"Packaged Mesa Dozen rejected Vulkan ICD negotiation.";
        }
        const auto report = VerifyVulkanInterface(dozen, getProc, L"Onyx Mesa Dozen", version);
        FreeLibrary(dozen);
        return report;
    }
    const DWORD dozenError = GetLastError();
    HMODULE loader = LoadPackagedLibrary(L"vulkan-1.dll", 0);
    if (!loader) {
        const DWORD loaderError = GetLastError();
        return L"No Vulkan ICD/loader packaged. Onyx vulkan_dzn.dll load error " +
            std::to_wstring(dozenError) + L"; vulkan-1.dll error " +
            std::to_wstring(loaderError) +
            L". Build the UWP Dozen DLL; a D3D12 clear is not a Vulkan device.";
    }
    const auto gipa = reinterpret_cast<GetProc>(
        GetProcAddress(loader, "vkGetInstanceProcAddr"));
    auto getVersion = reinterpret_cast<EnumerateInstanceVersion>(
        GetProcAddress(loader, "vkEnumerateInstanceVersion"));
    std::uint32_t apiVersion = (1u << 22);
    if (getVersion && getVersion(&apiVersion) != kVkSuccess)
        apiVersion = (1u << 22);
    const bool versionOkay = (apiVersion >> 22) > 1 ||
        ((apiVersion >> 22) == 1 && ((apiVersion >> 12) & 1023u) >= 1);
    if (!versionOkay) {
        FreeLibrary(loader);
        return L"Packaged Vulkan loader present, but API version is lower than 1.1.";
    }
    const auto report = VerifyVulkanInterface(loader, gipa, L"Packaged Vulkan loader", 0);
    FreeLibrary(loader);
    return report;
}
