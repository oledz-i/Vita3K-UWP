// SPDX-License-Identifier: GPL-3.0-or-later
#include "VitaVulkanBridge.h"
#include <stdexcept>
#include <cstring>

namespace {
void Check(VkResult result, const char* stage) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(stage) + ": VkResult " + std::to_string(result));
}
}
template<class T> T VitaVulkanBridge::InstanceProc(const char* name) const {
    auto fn = reinterpret_cast<T>(ipa_(instance_, name));
    if (!fn) throw std::runtime_error(std::string("Missing Vulkan instance entry: ") + name);
    return fn;
}
template<class T> T VitaVulkanBridge::DeviceProc(const char* name) const {
    auto fn = reinterpret_cast<T>(dpa_(device_, name));
    if (!fn) throw std::runtime_error(std::string("Missing Vulkan device entry: ") + name);
    return fn;
}
VitaVulkanBridge::~VitaVulkanBridge() { Shutdown(); }
bool VitaVulkanBridge::Accept(retro_hw_render_callback* cb) {
    if (!cb || cb->context_type != RETRO_HW_CONTEXT_VULKAN) return false;
    callback_ = *cb;
    return true;
}
bool VitaVulkanBridge::Negotiate(const retro_hw_render_context_negotiation_interface_vulkan* n) {
    if (!n || n->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN || !n->create_device) return false;
    negotiation_ = n;
    return true;
}
bool VitaVulkanBridge::Initialize(std::string& error) {
    if (device_) return true;
    try {
        if (!negotiation_ || !callback_.context_reset) throw std::runtime_error("Core did not supply Vulkan context negotiation");
        icd_ = LoadPackagedLibrary(L"vulkan_dzn.dll", 0);
        if (!icd_) throw std::runtime_error("Packaged Dozen ICD could not load: " + std::to_string(GetLastError()));
        using NegotiateIcd = VkResult(VKAPI_PTR*)(uint32_t*);
        auto negotiate = reinterpret_cast<NegotiateIcd>(GetProcAddress(icd_, "vk_icdNegotiateLoaderICDInterfaceVersion"));
        if (negotiate) { uint32_t version = 5; Check(negotiate(&version), "ICD negotiation"); }
        ipa_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(icd_, "vk_icdGetInstanceProcAddr"));
        if (!ipa_) throw std::runtime_error("Dozen ICD instance dispatch is missing");
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Vita3K UWP"; app.apiVersion = VK_API_VERSION_1_1;
        const auto* info = negotiation_->get_application_info ? negotiation_->get_application_info() : &app;
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; create.pApplicationInfo = info;
        Check(InstanceProc<PFN_vkCreateInstance>("vkCreateInstance")(&create, nullptr, &instance_), "Create instance");
        auto enumerate = InstanceProc<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
        uint32_t count = 0; Check(enumerate(instance_, &count, nullptr), "Enumerate GPUs");
        if (!count) throw std::runtime_error("Dozen reports no Vulkan GPU");
        std::vector<VkPhysicalDevice> devices(count); Check(enumerate(instance_, &count, devices.data()), "Enumerate GPUs");
        retro_vulkan_context context{};
        VkPhysicalDeviceFeatures required{};
        bool made = false;
        for (auto gpu : devices) {
            context = {};
            if (negotiation_->create_device(&context, instance_, gpu, VK_NULL_HANDLE, ipa_, nullptr, 0, nullptr, 0, &required)) { made = true; break; }
        }
        if (!made || !context.device || !context.queue) throw std::runtime_error("Vita3K could not create its Vulkan device with the required features");
        gpu_ = context.gpu; device_ = context.device; queue_ = context.queue; family_ = context.queue_family_index;
        dpa_ = InstanceProc<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex = family_;
        Check(DeviceProc<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device_, &pool, nullptr, &pool_), "Create readback pool");
        interface_.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
        interface_.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
        interface_.handle = this; interface_.instance = instance_; interface_.gpu = gpu_; interface_.device = device_;
        interface_.get_device_proc_addr = dpa_; interface_.get_instance_proc_addr = ipa_;
        interface_.queue = queue_; interface_.queue_index = family_;
        interface_.set_image = SetImage; interface_.get_sync_index = SyncIndex; interface_.get_sync_index_mask = SyncMask;
        interface_.set_command_buffers = Commands; interface_.wait_sync_index = Wait;
        interface_.lock_queue = Lock; interface_.unlock_queue = Unlock; interface_.set_signal_semaphore = Signal;
        reset_ = true;
        callback_.context_reset();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        // The caller unloads the core before Shutdown. A partially initialized
        // renderer can still own allocations on this device after context_reset.
        return false;
    }
}
void VitaVulkanBridge::SetImage(void* p, const retro_vulkan_image* image, uint32_t count, const VkSemaphore* waits, uint32_t family) {
    auto& self = *static_cast<VitaVulkanBridge*>(p);
    // The pinned Vita3K core submits on our graphics queue and passes IGNORED.
    // Reject unsupported ownership transfers rather than reading an unowned image.
    self.haveImage_ = image && (family == VK_QUEUE_FAMILY_IGNORED || family == self.family_);
    if (!self.haveImage_) return;
    self.image_ = *image;
    self.waits_.clear();
    if (count && waits) self.waits_.assign(waits, waits + count);
}
void VitaVulkanBridge::Commands(void* p, uint32_t n, const VkCommandBuffer* commands) {
    auto& self = *static_cast<VitaVulkanBridge*>(p);
    self.pending_.clear(); if (n && commands) self.pending_.assign(commands, commands + n);
}
void VitaVulkanBridge::Wait(void* p) {
    auto& self = *static_cast<VitaVulkanBridge*>(p);
    std::lock_guard<std::mutex> lock(self.queueMutex_);
    if (self.device_) self.DeviceProc<PFN_vkQueueWaitIdle>("vkQueueWaitIdle")(self.queue_);
}
bool VitaVulkanBridge::Present(unsigned width, unsigned height, std::string& error) {
    if (!device_ || !haveImage_ || !sink) return false;
    if (!width || !height || width > 4096 || height > 4096) { error = "Invalid core frame size"; return false; }
    const auto format = image_.create_info.format;
    const bool rgba = format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB;
    if (!rgba && format != VK_FORMAT_B8G8R8A8_UNORM && format != VK_FORMAT_B8G8R8A8_SRGB) { error = "Unsupported core image format"; return false; }
    std::lock_guard<std::mutex> lock(queueMutex_);
    if (gpuStalled_) { error = "GPU stopped responding earlier; restart the game"; return false; }
    VkCommandBuffer command = VK_NULL_HANDLE;
    bool submitted = false;
    try {
#define FN(name) DeviceProc<PFN_##name>(#name)
        const VkDeviceSize size = VkDeviceSize(width) * height * 4;
        if (size > readbackSize_) {
            ReleaseReadback();
            VkBufferCreateInfo bc{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bc.size = size; bc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            Check(FN(vkCreateBuffer)(device_, &bc, nullptr, &readback_), "Create readback buffer");
            VkMemoryRequirements requirements{}; FN(vkGetBufferMemoryRequirements)(device_, readback_, &requirements);
            VkPhysicalDeviceMemoryProperties properties{};
            InstanceProc<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(gpu_, &properties);
            // GPU-to-CPU copies belong in cached host memory (D3D12's readback
            // heap). The first host-visible type Dozen lists is write-combined
            // upload memory, which the GPU is not meant to copy into and the
            // CPU reads very slowly: frames read from it can come back black.
            uint32_t type = properties.memoryTypeCount;
            const VkMemoryPropertyFlags visible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            const VkMemoryPropertyFlags cached = VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            const VkMemoryPropertyFlags coherent = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            const VkMemoryPropertyFlags preferences[] = { visible | cached | coherent, visible | cached, visible | coherent, visible };
            for (const auto wanted : preferences) {
                for (uint32_t i = 0; i < properties.memoryTypeCount && type == properties.memoryTypeCount; ++i)
                    if ((requirements.memoryTypeBits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & wanted) == wanted) type = i;
                if (type != properties.memoryTypeCount) break;
            }
            if (type == properties.memoryTypeCount) throw std::runtime_error("No host-visible readback memory");
            readbackCoherent_ = (properties.memoryTypes[type].propertyFlags & coherent) != 0;
            const auto flags = properties.memoryTypes[type].propertyFlags;
            readbackInfo_ = "readback memory type " + std::to_string(type) + " of " + std::to_string(properties.memoryTypeCount) +
                ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? ", device-local" : "") + ((flags & cached) ? ", cached" : ", uncached") +
                ((flags & coherent) ? ", coherent" : ", not coherent") + "; image format " + std::to_string(format) +
                ", layout " + std::to_string(image_.image_layout);
            VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocate.allocationSize = requirements.size; allocate.memoryTypeIndex = type;
            Check(FN(vkAllocateMemory)(device_, &allocate, nullptr, &readbackMemory_), "Allocate readback memory");
            Check(FN(vkBindBufferMemory)(device_, readback_, readbackMemory_, 0), "Bind readback buffer");
            Check(FN(vkMapMemory)(device_, readbackMemory_, 0, VK_WHOLE_SIZE, 0, &readbackMapped_), "Map readback");
            readbackSize_ = size;
        }
        if (!fence_) {
            VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            Check(FN(vkCreateFence)(device_, &fc, nullptr, &fence_), "Create readback fence");
        }
        Check(FN(vkResetFences)(device_, 1, &fence_), "Reset readback fence");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool = pool_; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 1;
        Check(FN(vkAllocateCommandBuffers)(device_, &ca, &command), "Allocate readback command");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Check(FN(vkBeginCommandBuffer)(command, &begin), "Begin readback");
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.image = image_.create_info.image; barrier.oldLayout = image_.image_layout;
        barrier.newLayout = barrier.oldLayout == VK_IMAGE_LAYOUT_GENERAL ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange = image_.create_info.subresourceRange;
        barrier.subresourceRange.levelCount = 1; barrier.subresourceRange.layerCount = 1;
        FN(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, barrier.subresourceRange.baseMipLevel, barrier.subresourceRange.baseArrayLayer, 1}; copy.imageExtent = {width, height, 1};
        FN(vkCmdCopyImageToBuffer)(command, barrier.image, barrier.newLayout, readback_, 1, &copy);
        auto layout = barrier.oldLayout; barrier.oldLayout = barrier.newLayout; barrier.newLayout = layout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT; host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; host.buffer = readback_; host.size = VK_WHOLE_SIZE;
        FN(vkCmdPipelineBarrier)(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &host, 1, &barrier);
        Check(FN(vkEndCommandBuffer)(command), "End readback");
        pending_.push_back(command);
        std::vector<VkPipelineStageFlags> stages(waits_.size(), VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount = static_cast<uint32_t>(waits_.size()); submit.pWaitSemaphores = waits_.data(); submit.pWaitDstStageMask = stages.data(); submit.commandBufferCount = static_cast<uint32_t>(pending_.size()); submit.pCommandBuffers = pending_.data();
        if (signal_) { submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &signal_; }
        Check(FN(vkQueueSubmit)(queue_, 1, &submit, fence_), "Submit readback"); submitted = true;
        // Never wait forever: a hung GPU frame becomes a logged error, not a frozen app.
        const VkResult waited = FN(vkWaitForFences)(device_, 1, &fence_, VK_TRUE, 2000000000ull);
        if (waited == VK_TIMEOUT) {
            gpuStalled_ = true; // the command buffer may still be in use: leave it
            waits_.clear(); pending_.clear(); signal_ = VK_NULL_HANDLE; haveImage_ = false;
            error = "GPU did not finish a frame within 2 seconds";
            return false;
        }
        Check(waited, "Wait for readback");
        if (!readbackCoherent_) {
            VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE}; range.memory = readbackMemory_; range.size = VK_WHOLE_SIZE;
            Check(FN(vkInvalidateMappedMemoryRanges)(device_, 1, &range), "Invalidate readback");
        }
        sink(static_cast<const uint8_t*>(readbackMapped_), width, height, rgba);
        FN(vkFreeCommandBuffers)(device_, pool_, 1, &command);
        waits_.clear(); pending_.clear(); signal_ = VK_NULL_HANDLE; haveImage_ = false;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        if (submitted) {
            // Bounded: if the GPU is gone, do not hang here either.
            if (FN(vkWaitForFences)(device_, 1, &fence_, VK_TRUE, 2000000000ull) != VK_SUCCESS) gpuStalled_ = true;
        }
        if (command && !gpuStalled_) FN(vkFreeCommandBuffers)(device_, pool_, 1, &command);
        pending_.clear(); waits_.clear(); signal_ = VK_NULL_HANDLE; haveImage_ = false;
        return false;
    }
#undef FN
}
void VitaVulkanBridge::ReleaseReadback() {
    if (!device_) return;
    if (readbackMapped_) DeviceProc<PFN_vkUnmapMemory>("vkUnmapMemory")(device_, readbackMemory_);
    if (readback_) DeviceProc<PFN_vkDestroyBuffer>("vkDestroyBuffer")(device_, readback_, nullptr);
    if (readbackMemory_) DeviceProc<PFN_vkFreeMemory>("vkFreeMemory")(device_, readbackMemory_, nullptr);
    readbackMapped_ = nullptr; readback_ = VK_NULL_HANDLE; readbackMemory_ = VK_NULL_HANDLE; readbackSize_ = 0;
}
void VitaVulkanBridge::Shutdown() {
    try {
        // After a stalled frame, waiting for the device could hang the app.
        if (device_ && !gpuStalled_) DeviceProc<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(device_);
        if (reset_ && callback_.context_destroy) callback_.context_destroy();
        reset_ = false;
        ReleaseReadback();
        if (device_ && fence_) DeviceProc<PFN_vkDestroyFence>("vkDestroyFence")(device_, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
        gpuStalled_ = false;
        if (device_ && pool_) DeviceProc<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device_, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
        if (device_ && negotiation_ && negotiation_->destroy_device) negotiation_->destroy_device();
        if (device_) DeviceProc<PFN_vkDestroyDevice>("vkDestroyDevice")(device_, nullptr);
        device_ = VK_NULL_HANDLE;
        if (instance_) InstanceProc<PFN_vkDestroyInstance>("vkDestroyInstance")(instance_, nullptr);
    } catch (...) {}
    device_ = VK_NULL_HANDLE; instance_ = VK_NULL_HANDLE; queue_ = VK_NULL_HANDLE; haveImage_ = false;
    negotiation_ = nullptr; callback_ = {}; waits_.clear(); pending_.clear(); signal_ = VK_NULL_HANDLE;
    if (icd_) FreeLibrary(icd_);
    icd_ = nullptr; ipa_ = nullptr; dpa_ = nullptr;
}
