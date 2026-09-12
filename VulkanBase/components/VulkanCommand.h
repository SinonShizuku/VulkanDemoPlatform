#pragma once
#include "../../Start.h"
#include "../VulkanCore.h"
#include "VulkanSync.h"
#include "../Benchmark.h"

class VulkanCommandBuffer {
    friend class VulkanCommandPool;
    VkCommandBuffer handle = VK_NULL_HANDLE;
public:
    VulkanCommandBuffer() = default;
    VulkanCommandBuffer(VulkanCommandBuffer &&other) noexcept {MoveHandle};

    // getter
    DefineHandleTypeOperator;
    DefineAddressFunction;

    // const function
    result_t begin(VkCommandBufferUsageFlags usage_flags, VkCommandBufferInheritanceInfo &inheritance_info) const {
        inheritance_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = usage_flags,
            .pInheritanceInfo = &inheritance_info
        };
        VkResult result = vkBeginCommandBuffer(handle, &begin_info);
        if (result)
            outstream << std::format("[ VulkanCommandBuffer ] ERROR\nFailed to begin a command buffer!\nError code: {}\n", int32_t(result));
        else
            benchmark_hook_begin(handle);  // benchmark 时在帧命令缓冲开头写 timestamp
        return result;
    }

    result_t begin(VkCommandBufferUsageFlags usage_flags = 0) const {
        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = usage_flags
        };
        VkResult result = vkBeginCommandBuffer(handle, &begin_info);
        if (result)
            outstream << std::format("[ VulkanCommandBuffer ] ERROR\nFailed to begin a command buffer!\nError code: {}\n", int32_t(result));
        else
            benchmark_hook_begin(handle);  // benchmark 时在帧命令缓冲开头写 timestamp
        return result;
    }

    result_t end() const {
        benchmark_hook_end(handle);  // benchmark 时在帧命令缓冲结尾写 timestamp
        VkResult result = vkEndCommandBuffer(handle);
        if (result)
            outstream << std::format("[ VulkanCommandBuffer ] ERROR\nFailed to end a command buffer!\nError code: {}\n", int32_t(result));
        return result;
    }
};

class VulkanCommandPool {
    VkCommandPool handle = VK_NULL_HANDLE;
public:
    VulkanCommandPool() = default;
    VulkanCommandPool(VkCommandPoolCreateInfo& createInfo) {
        create(createInfo);
    }
    VulkanCommandPool(uint32_t queue_family_index, VkCommandPoolCreateFlags flags = 0) {
        create(queue_family_index, flags);
    }
    VulkanCommandPool(VulkanCommandPool&& other) noexcept { MoveHandle; }
    ~VulkanCommandPool() { DestroyHandleBy(VulkanCore::get_singleton().get_vulkan_device().get_device(),vkDestroyCommandPool); }
    //Getter
    DefineHandleTypeOperator;
    DefineAddressFunction;
    //Const Function
    result_t allocate_buffers(array_ref<VkCommandBuffer> buffers, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) const {
        VkCommandBufferAllocateInfo allocateInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = handle,
            .level = level,
            .commandBufferCount = uint32_t(buffers.Count())
        };
        VkResult result = vkAllocateCommandBuffers(VulkanCore::get_singleton().get_vulkan_device().get_device(), &allocateInfo, buffers.Pointer());
        if (result)
            outstream << std::format("[ commandPool ] ERROR\nFailed to allocate command buffers!\nError code: {}\n", int32_t(result));
        return result;
    }
    result_t allocate_buffers(array_ref<VulkanCommandBuffer> buffers, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) const {
        return allocate_buffers(
            { &buffers[0].handle, buffers.Count() },
            level);
    }
    void free_buffers(array_ref<VkCommandBuffer> buffers) const {
        vkFreeCommandBuffers(VulkanCore::get_singleton().get_vulkan_device().get_device(), handle, buffers.Count(), buffers.Pointer());
        memset(buffers.Pointer(), 0, buffers.Count() * sizeof(VkCommandBuffer));
    }
    void free_buffers(array_ref<VulkanCommandBuffer> buffers) const {
        free_buffers({ &buffers[0].handle, buffers.Count() });
    }
    //Non-const Function
    result_t create(VkCommandPoolCreateInfo& createInfo) {
        createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        VkResult result = vkCreateCommandPool(VulkanCore::get_singleton().get_vulkan_device().get_device(), &createInfo, nullptr, &handle);
        if (result)
            outstream << std::format("[ commandPool ] ERROR\nFailed to create a command pool!\nError code: {}\n", int32_t(result));
        return result;
    }
    result_t create(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0) {
        VkCommandPoolCreateInfo createInfo = {
            .flags = flags,
            .queueFamilyIndex = queueFamilyIndex
        };
        return create(createInfo);
    }
};

class VulkanCommand {
private:
    VulkanCommandPool command_pool_graphics;
    VulkanCommandPool command_pool_presentation;
    VulkanCommandPool command_pool_compute;
    VulkanCommandBuffer command_buffer_transfer;
    VulkanCommandBuffer command_buffer_presentation;

public:
    VulkanCommand() {
        auto initialize = [this] {
            if (VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_graphics() != VK_QUEUE_FAMILY_IGNORED) {
                command_pool_graphics.create(VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_graphics(),VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
                command_pool_graphics.allocate_buffers(command_buffer_transfer);
            }
            if (VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_compute() != VK_QUEUE_FAMILY_IGNORED) {
                command_pool_compute.create(VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_compute(),VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
            }
            if (VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_presentation() != VK_QUEUE_FAMILY_IGNORED &&
                VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_presentation() != VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_graphics() &&
                VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageSharingMode == VK_SHARING_MODE_EXCLUSIVE) {
                command_pool_presentation.create(VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_presentation(),VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT),
                command_pool_presentation.allocate_buffers(command_buffer_presentation);
                }
            VulkanCore::get_singleton().get_vulkan_device().set_physical_device_format_properties();
        };
        auto clean_up = [this] {
            command_pool_graphics.~VulkanCommandPool();
            command_pool_presentation.~VulkanCommandPool();
            command_pool_compute.~VulkanCommandPool();
        };
        VulkanCore::get_singleton().get_vulkan_device().add_callback_create_device(initialize);
        VulkanCore::get_singleton().get_vulkan_device().add_callback_destory_device(clean_up);
    }
    ~VulkanCommand() {}

    static VulkanCommand& get_singleton() {
        static VulkanCommand singleton = VulkanCommand();
        return singleton;
    }

    // getter
    [[nodiscard]] const VulkanCommandPool & get_command_pool_graphics() const {
        return command_pool_graphics;
    }

    [[nodiscard]] const VulkanCommandPool & get_command_pool_compute() const {
        return command_pool_compute;
    }

    [[nodiscard]] const VulkanCommandBuffer & get_command_buffer_transfer() const {
        return command_buffer_transfer;
    }

    static result_t submit_command_buffer_graphics(VkSubmitInfo &submit_info, VkFence fence = VK_NULL_HANDLE) {
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkResult result = vkQueueSubmit(VulkanCore::get_singleton().get_vulkan_device().get_queue_graphics(),1,&submit_info,fence);
        if (result)
            outstream << std::format("[ VulkanExecutionManager ] ERROR\nFailed to submit the command buffer!\nError code: {}\n", int32_t(result));
        return result;
    }

    result_t submit_command_buffer_graphics(const VkCommandBuffer command_buffer,
                                            VkSemaphore semaphore_image_is_available = VK_NULL_HANDLE, VkSemaphore semaphore_render_is_over = VK_NULL_HANDLE, VkFence fence = VK_NULL_HANDLE,
                                            VkPipelineStageFlags wait_Dst_stage_image_is_available = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT ) const {
        VkSubmitInfo submit_info = {
            .commandBufferCount = 1,
            .pCommandBuffers = &command_buffer,
        };
        if (semaphore_image_is_available) {
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &semaphore_image_is_available;
            submit_info.pWaitDstStageMask = &wait_Dst_stage_image_is_available;
        }
        if (semaphore_render_is_over) {
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &semaphore_render_is_over;
        }
        return submit_command_buffer_graphics(submit_info, fence);
    }

    result_t submit_command_buffer_graphics(const VkCommandBuffer command_buffer, VkFence fence = VK_NULL_HANDLE) const {
        VkSubmitInfo submit_info = {
            .commandBufferCount = 1,
            .pCommandBuffers = &command_buffer,
        };
        return submit_command_buffer_graphics(submit_info, fence);
    }

    result_t submit_command_buffer_compute(VkSubmitInfo &submit_info, VkFence fence = VK_NULL_HANDLE) const {
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        VkResult result = vkQueueSubmit(VulkanCore::get_singleton().get_vulkan_device().get_queue_compute(),1,&submit_info,fence);
        if (result)
            outstream << std::format("[ VulkanExecutionManager ] ERROR\nFailed to submit the command buffer!\nError code: {}\n", int32_t(result));
        return result;
    }

    result_t submit_command_buffer_compute(const VkCommandBuffer command_buffer, VkFence fence = VK_NULL_HANDLE) const {
        VkSubmitInfo submit_info = {
            .commandBufferCount = 1,
            .pCommandBuffers = &command_buffer,
        };
        return submit_command_buffer_compute(submit_info, fence);
    }

    result_t submit_command_buffer_presentation(VkCommandBuffer command_buffer,
    VkSemaphore semaphore_rendering_is_over = VK_NULL_HANDLE, VkSemaphore semaphore_ownership_is_transfered = VK_NULL_HANDLE, VkFence fence = VK_NULL_HANDLE) {
        static constexpr VkPipelineStageFlags wait_Dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &command_buffer
        };
        if (semaphore_rendering_is_over) {
            submit_info.waitSemaphoreCount = 1;
            submit_info.pWaitSemaphores = &semaphore_rendering_is_over;
            submit_info.pWaitDstStageMask = &wait_Dst_stage;
        }
        if (semaphore_ownership_is_transfered) {
            submit_info.signalSemaphoreCount = 1;
            submit_info.pSignalSemaphores = &semaphore_ownership_is_transfered;
        }
        VkResult result = vkQueueSubmit(VulkanCore::get_singleton().get_vulkan_device().get_queue_presentation(),1,&submit_info,fence);
        if (result)
            outstream << std::format("[ VulkanExecutionManager ] ERROR\nFailed to submit the presentation command buffer!\nError code: {}\n", int32_t(result));
        return result;
    }

    result_t present_image(VkPresentInfoKHR &present_info) {
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        switch (VkResult result = vkQueuePresentKHR(VulkanCore::get_singleton().get_vulkan_device().get_queue_presentation(),&present_info)) {
            case VK_SUCCESS:
                return VK_SUCCESS;
            case VK_SUBOPTIMAL_KHR:
            case VK_ERROR_OUT_OF_DATE_KHR:
                return VulkanSwapchainManager::get_singleton().recreate_swapchain();
            default:
                outstream << std::format("[ VulkanExecutionManager ] ERROR\nFailed to queue the image for presentation!\nError code: {}\n", int32_t(result));
                return result;
        }
    }

    result_t present_image(VkSemaphore semaphore_rendering_is_over = VK_NULL_HANDLE) {
        auto &swapchain = VulkanSwapchainManager::get_singleton().get_swapchain();
        auto &current_image_index = VulkanSwapchainManager::get_singleton().get_current_image_index();
        VkPresentInfoKHR present_info = {
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &current_image_index
        };
        if (semaphore_rendering_is_over) {
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &semaphore_rendering_is_over;
        }
        return present_image(present_info);
    }

    void cmd_transfer_image_ownership(VkCommandBuffer command_buffer) {
        VkImageMemoryBarrier image_memory_barrier_g2p = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = 0,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_graphics(),
        .dstQueueFamilyIndex = VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_presentation(),
        .image = VulkanSwapchainManager::get_singleton().get_swapchain_image(VulkanSwapchainManager::get_singleton().get_current_image_index()),
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
        vkCmdPipelineBarrier(command_buffer,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
        0, nullptr, 0, nullptr, 1,&image_memory_barrier_g2p);
    }

    result_t execute_command_buffer_graphics(VkCommandBuffer command_buffer) {
        fence fence;
        VkSubmitInfo submit_info = {
            .commandBufferCount = 1,
            .pCommandBuffers = &command_buffer
        };
        VkResult result = submit_command_buffer_graphics(submit_info, fence);
        if (!result) fence.wait();
        return result;
    }

    result_t acquire_image_ownership_presentation(VkSemaphore semaphore_rendering_is_over, VkSemaphore semaphore_ownership_is_transfered, VkFence fence = VK_NULL_HANDLE) {
        if (VkResult result = command_buffer_presentation.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
            return result;
        cmd_transfer_image_ownership(command_buffer_presentation);
        if (VkResult result = command_buffer_presentation.end())
            return result;
        return submit_command_buffer_presentation(command_buffer_presentation,semaphore_rendering_is_over,semaphore_ownership_is_transfered,fence);
    }

};