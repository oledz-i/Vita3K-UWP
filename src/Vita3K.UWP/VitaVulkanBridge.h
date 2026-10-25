// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#define VK_NO_PROTOTYPES
#include <libretro_vulkan.h>
#include <windows.h>
#include <functional>
#include <mutex>
#include <vector>
#include <string>

// One synchronous offscreen image at a time. No Win32 window or Vulkan WSI.
class VitaVulkanBridge {
public:
    using FrameSink = std::function<void(const uint8_t*, unsigned, unsigned, bool)>;
    ~VitaVulkanBridge();
    bool Accept(retro_hw_render_callback* callback);
    bool Negotiate(const retro_hw_render_context_negotiation_interface_vulkan* negotiation);
    bool Initialize(std::string& error);
    void Shutdown();
    bool Present(unsigned width, unsigned height, std::string& error);
    const retro_hw_render_interface_vulkan* Interface() const { return device_ ? &interface_ : nullptr; }
    FrameSink sink;
private:
    template<class T> T InstanceProc(const char* name) const;
    template<class T> T DeviceProc(const char* name) const;
    HMODULE icd_ = nullptr;
    PFN_vkGetInstanceProcAddr ipa_ = nullptr;
    PFN_vkGetDeviceProcAddr dpa_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t family_ = 0;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    retro_hw_render_callback callback_{};
    const retro_hw_render_context_negotiation_interface_vulkan* negotiation_ = nullptr;
    retro_hw_render_interface_vulkan interface_{};
    retro_vulkan_image image_{};
    bool haveImage_ = false;
    bool reset_ = false;
    std::mutex queueMutex_;
    std::vector<VkSemaphore> waits_;
    std::vector<VkCommandBuffer> pending_;
    VkSemaphore signal_ = VK_NULL_HANDLE;
    // Readback buffer and fence kept between frames; rebuilt when the frame grows.
    VkBuffer readback_ = VK_NULL_HANDLE;
    VkDeviceMemory readbackMemory_ = VK_NULL_HANDLE;
    VkDeviceSize readbackSize_ = 0;
    void* readbackMapped_ = nullptr;
    VkFence fence_ = VK_NULL_HANDLE;
    bool gpuStalled_ = false;
    void ReleaseReadback();
    static void SetImage(void*, const retro_vulkan_image*, uint32_t, const VkSemaphore*, uint32_t);
    static uint32_t SyncIndex(void*) { return 0; }
    static uint32_t SyncMask(void*) { return 1; }
    static void Commands(void*, uint32_t, const VkCommandBuffer*);
    static void Wait(void*);
    static void Lock(void* p) { static_cast<VitaVulkanBridge*>(p)->queueMutex_.lock(); }
    static void Unlock(void* p) { static_cast<VitaVulkanBridge*>(p)->queueMutex_.unlock(); }
    static void Signal(void* p, VkSemaphore s) { static_cast<VitaVulkanBridge*>(p)->signal_ = s; }
};
