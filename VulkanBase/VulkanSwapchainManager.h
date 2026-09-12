#pragma once
#include "VulkanCore.h"
#include "../Start.h"
#include "components/VulkanSync.h"
#include <functional>

class VulkanSwapchainManager {
public:
    // swapchain 回调带 owner：注册者（例如某个 demo）在销毁前必须调用
    // remove_swapchain_callbacks(owner) 注销，否则回调会在 swapchain 重建/退出时访问已析构的对象。
    struct SwapchainCallback {
        const void* owner = nullptr;
        std::function<void()> function;
    };

    VulkanSwapchainManager() {
        vulkan_device = &VulkanCore::get_singleton().get_vulkan_device();
        vulkan_surface = &VulkanCore::get_singleton().get_vulkan_instance().get_surface();
        swapchain = VK_NULL_HANDLE;
    }
    ~VulkanSwapchainManager(){};

    void destroy_singleton() {
        wait_idle();
        if (swapchain) {
            for (auto& i : callbacks_destroy_swapchain)
                i.function();
            for (auto& i : swapchain_image_views)
                if (i)
                    vkDestroyImageView(vulkan_device->get_device(), i, nullptr);
            vkDestroySwapchainKHR(vulkan_device->get_device(), swapchain, nullptr);
        }
        swapchain = VK_NULL_HANDLE;
        swapchain_images.resize(0);
        swapchain_image_views.resize(0);
        render_finished_semaphores.clear();
        swapchain_create_info = {};
    }

    static VulkanSwapchainManager& get_singleton() {
        static VulkanSwapchainManager singleton = VulkanSwapchainManager();
        return singleton;
    }

    // getter
    [[nodiscard]] const VkFormat &get_available_surface_format(uint32_t index) const {
        return available_surface_formats[index].format;
    }

    [[nodiscard]] const VkColorSpaceKHR &get_available_surface_color_space(uint32_t index) const {
        return available_surface_formats[index].colorSpace;
    }

    [[nodiscard]] uint32_t get_available_surface_format_count() const {
        return uint32_t(available_surface_formats.size());
    }

    [[nodiscard]] VkSwapchainKHR &get_swapchain() {
        return swapchain;
    }

    [[nodiscard]] VkImage get_swapchain_image(uint32_t index) const{
        return swapchain_images[index];
    }

    [[nodiscard]] VkImageView get_swapchain_image_view(uint32_t index) const {
        return swapchain_image_views[index];
    }

    // 每张 swapchain image 一个 render-finished semaphore：presentation engine 可能仍在
    // 使用上一张 image 的信号量，复用同一个会触发
    // VUID-vkQueueSubmit-pSignalSemaphores-00067（swapchain semaphore reuse）。
    [[nodiscard]] VkSemaphore get_render_finished_semaphore(uint32_t index) const {
        return index < render_finished_semaphores.size()
                   ? static_cast<VkSemaphore>(render_finished_semaphores[index])
                   : VK_NULL_HANDLE;
    }

    [[nodiscard]] uint32_t get_swapchain_image_count() const {
        return uint32_t(swapchain_images.size());
    }

    [[nodiscard]] VkSwapchainCreateInfoKHR& get_swapchain_create_info(){
        return swapchain_create_info;
    }

    [[nodiscard]] std::vector<SwapchainCallback> & get_callbacks_create_swapchain() {
        return callbacks_create_swapchain;
    }

    [[nodiscard]] std::vector<SwapchainCallback> & get_callbacks_destroy_swapchain() {
        return callbacks_destroy_swapchain;
    }

    [[nodiscard]] std::vector<VkImageView> & get_swapchain_image_views() {
        return swapchain_image_views;
    }

    [[nodiscard]] std::vector<VkSurfaceFormatKHR> & get_available_surface_formats() {
        return available_surface_formats;
    }

    [[nodiscard]] uint32_t &get_current_image_index(){
        return current_image_index;
    }

    // setter
    void set_swapchain(VkSwapchainKHR swapchain) {
        this->swapchain = swapchain;
    }

    void add_callback_create_swapchain(std::function<void()> function, const void* owner = nullptr) {
        callbacks_create_swapchain.push_back({ owner, std::move(function) });
    }

    void add_callback_destroy_swapchain(std::function<void()> function, const void* owner = nullptr) {
        callbacks_destroy_swapchain.push_back({ owner, std::move(function) });
    }

    // 注销 owner 注册的全部 swapchain 回调。
    void remove_swapchain_callbacks(const void* owner) {
        auto remove_owner = [owner](std::vector<SwapchainCallback>& callbacks) {
            std::erase_if(callbacks, [owner](const SwapchainCallback& callback) { return callback.owner == owner; });
        };
        remove_owner(callbacks_create_swapchain);
        remove_owner(callbacks_destroy_swapchain);
    }
    
    void clear_all_callbacks() {
        callbacks_create_swapchain.clear();
        callbacks_destroy_swapchain.clear();
    }

    result_t wait_idle() const {
        result_t result = vkDeviceWaitIdle(vulkan_device->get_device());
        if (result)
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to wait for the device to be idle!\nError code: {}\n", int32_t(result));
        return result;
    }

    result_t set_surface_formats(VkSurfaceFormatKHR surface_format, VkSwapchainCreateInfoKHR &swapchain_create_info) {
        bool format_is_available = false;
        if (!surface_format.format) {
            for (auto &i : available_surface_formats) {
                if (i.colorSpace == surface_format.colorSpace) {
                    swapchain_create_info.imageFormat = i.format;
                    swapchain_create_info.imageColorSpace = i.colorSpace;
                    format_is_available = true;
                    break;
                }
            }
        }
        else {
            for (auto &i : available_surface_formats) {
                if (i.colorSpace == surface_format.colorSpace && i.format == surface_format.format) {
                    swapchain_create_info.imageFormat = i.format;
                    swapchain_create_info.imageColorSpace = i.colorSpace;
                    format_is_available = true;
                    break;
                }
            }
        }
        if (!format_is_available)
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        if (swapchain)
            return recreate_swapchain();
        return VK_SUCCESS;
    }

    result_t get_surface_formats() {
        uint32_t surfaceFormatCount;
        if (result_t result = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan_device->get_physical_device(), *vulkan_surface, &surfaceFormatCount, nullptr)) {
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to get the count of surface formats!\nError code: {}\n", int32_t(result));
            return result;
        }
        if (!surfaceFormatCount)
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to find any supported surface format!\n"),
            abort();
        available_surface_formats.resize(surfaceFormatCount);
        result_t result = vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan_device->get_physical_device(), *vulkan_surface, &surfaceFormatCount, available_surface_formats.data());
        if (result)
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to get surface formats!\nError code: {}\n", int32_t(result));
        return result;
    }

    result_t create_swapchain(bool limit_frame_rate = true, VkSwapchainCreateFlagsKHR flags = 0) {
        VkSurfaceCapabilitiesKHR surface_capabilities = {};

        if (result_t result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan_device->get_physical_device(), *vulkan_surface, &surface_capabilities); result != VK_SUCCESS) {
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to get physical device surface capabilities!\nError code: {}\n", int32_t(result));
            return result;
        }
        swapchain_create_info.minImageCount = surface_capabilities.minImageCount + (surface_capabilities.maxImageCount > surface_capabilities.minImageCount);
        swapchain_create_info.imageExtent =
            surface_capabilities.currentExtent.width == -1 ?
            VkExtent2D{
                glm::clamp(default_window_size.width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width),
                glm::clamp(default_window_size.height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height)}:
            surface_capabilities.currentExtent;
        swapchain_create_info.imageArrayLayers = 1;
        swapchain_create_info.preTransform = surface_capabilities.currentTransform;
        if (surface_capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
            swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        else
            for (size_t i=0;i<4;i++) {
                if (surface_capabilities.supportedCompositeAlpha & 1 << i) {
                    swapchain_create_info.compositeAlpha = VkCompositeAlphaFlagBitsKHR(surface_capabilities.supportedCompositeAlpha & 1 << i);
                    break;
                }
            }
        swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            swapchain_create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (surface_capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            swapchain_create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        else
            outstream << std::format("[ VulkanSwapchainManager ] WARNING\nVK_IMAGE_USAGE_TRANSFER_DST_BIT isn't supported!\n");

        if (available_surface_formats.empty())
            if (result_t result = get_surface_formats())
                return result;

        if (!swapchain_create_info.imageFormat) {
            if (set_surface_formats({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},swapchain_create_info) &&
                set_surface_formats({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},swapchain_create_info)) {
                swapchain_create_info.imageFormat = available_surface_formats[0].format;
                swapchain_create_info.imageColorSpace = available_surface_formats[0].colorSpace;
                outstream << std::format("[ VulkanSwapchainManager ] WARNING\nFailed to select a four-component UNORM surface format!\n");
            }
        }

        uint32_t surface_present_mode_count = 0;
        if (result_t result = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_device->get_physical_device(), *vulkan_surface, &surface_present_mode_count, nullptr)) {
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to get the count of surface present modes!\nError code: {}\n", int32_t(result));
            return result;
        }
        if (!surface_present_mode_count)
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to find any surface present mode!\n"),
            abort();
        std::vector<VkPresentModeKHR> surface_present_modes(surface_present_mode_count);
        if (result_t result = vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_device->get_physical_device(), *vulkan_surface, &surface_present_mode_count, surface_present_modes.data())) {
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to get surface present modes!\nError code: {}\n", int32_t(result));
            return result;
        }
        swapchain_create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (!limit_frame_rate)
            for (size_t i = 0; i < surface_present_mode_count; i++)
                if (surface_present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                    swapchain_create_info.presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }

        swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchain_create_info.flags = flags;
        swapchain_create_info.surface = *vulkan_surface;
        swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchain_create_info.clipped = VK_TRUE;
        swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;

        if (result_t result = create_swapchain_internal())
            return result;

        for (auto& i : callbacks_create_swapchain)
            i.function();

        return VK_SUCCESS;
    }

    result_t recreate_swapchain() {
        VkSurfaceCapabilitiesKHR surface_capabilities = {};

        if (result_t result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan_device->get_physical_device(), *vulkan_surface, &surface_capabilities)) {
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to get physical device surface capabilities!\nError code: {}\n", int32_t(result));
            return result;
        }
        if (surface_capabilities.currentExtent.width == 0 ||
            surface_capabilities.currentExtent.height == 0)
            return VK_SUBOPTIMAL_KHR;
        swapchain_create_info.imageExtent = surface_capabilities.currentExtent;

        swapchain_create_info.oldSwapchain = swapchain;

        result_t result = vkQueueWaitIdle(vulkan_device->get_queue_graphics());
        if (!result && vulkan_device->get_queue_graphics()!= vulkan_device->get_queue_presentation())
            result = vkQueueWaitIdle(vulkan_device->get_queue_presentation());
        if (result) {
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to wait for the queue to be idle!\nError code: {}\n", int32_t(result));
            return result;
        }

        for (auto& i : callbacks_destroy_swapchain)
            i.function();
        outstream << this->get_swapchain_image_views().size();

        // 销毁旧ImageViews
        for (auto&i:swapchain_image_views)
            if (i) vkDestroyImageView(vulkan_device->get_device(), i, nullptr);

        swapchain_image_views.resize(0);

        if (result_t result = create_swapchain_internal())
            return result;
        for (auto& i : callbacks_create_swapchain)
            i.function();
        return VK_SUCCESS;
    }

    result_t recreate_device(uint32_t api_version, VkDeviceCreateFlags flags = 0) {
        if (result_t result = wait_idle()) {
            return result;
        }
        if (swapchain) {
            for (auto& i : callbacks_destroy_swapchain)
                i.function();
            for (auto &i:swapchain_image_views)
                if (i) vkDestroyImageView(vulkan_device->get_device(), i, nullptr);
            swapchain_image_views.resize(0);
            render_finished_semaphores.clear();
            vkDestroySwapchainKHR(vulkan_device->get_device(), swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
            swapchain_create_info = {};
        }
        for (auto& i : vulkan_device->get_callbacks_destroy_device())
            i();
        if (auto device = vulkan_device->get_device())//防函数执行失败后再次调用时发生重复销毁
            vkDestroyDevice(device, nullptr),
            vulkan_device->set_device(VK_NULL_HANDLE);
        return vulkan_device->create_device(api_version, flags);
    }

    result_t swap_image(VkSemaphore semaphore_image_is_available) {
        if (swapchain_create_info.oldSwapchain && swapchain_create_info.oldSwapchain != swapchain) {
            vkDestroySwapchainKHR(vulkan_device->get_device(), swapchain_create_info.oldSwapchain, nullptr);
            swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;
        }
        // 上一帧发现 swapchain 已 suboptimal：此时上一帧的 fence 已等待过，可以安全重建。
        if (swapchain_recreate_pending) {
            swapchain_recreate_pending = false;
            if (VkResult result = recreate_swapchain())
                return result;
        }

        while (VkResult result = vkAcquireNextImageKHR(vulkan_device->get_device(), swapchain, UINT64_MAX, semaphore_image_is_available, VK_NULL_HANDLE, &current_image_index)) {
            switch (result) {
                case VK_SUBOPTIMAL_KHR:
                    // SUBOPTIMAL 表示“图像已经 acquire 成功、但 swapchain 需要重建”：
                    // 本帧照常使用这张图像，重建推迟到下一帧的 acquire 之前，
                    // 否则会用一张“从未 acquire”的新 swapchain 图像渲染（validation 会直接报错）。
                    swapchain_recreate_pending = true;
                    break;
                case VK_ERROR_OUT_OF_DATE_KHR:
                    if (VkResult recreate_result = recreate_swapchain())
                        return recreate_result;
                    continue; // 重建成功后重新 acquire
                default:
                    outstream << std::format("[ graphicsBase ] ERROR\nFailed to acquire the next image!\nError code: {}\n", int32_t(result));
                    return result;
            }
            break;
        }
        return VK_SUCCESS;
    }

    void add_next_structure_swapchain_create_info(auto& next, bool allow_duplicate = false) {
        set_pnext(const_cast<void*&>(swapchain_create_info.pNext), &next, allow_duplicate);
    }


private:
    VulkanDevice *vulkan_device;
    VkSurfaceKHR *vulkan_surface;

    std::vector<VkSurfaceFormatKHR> available_surface_formats;

    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;
    std::vector<semaphore> render_finished_semaphores;
    VkSwapchainCreateInfoKHR swapchain_create_info = {};

    std::vector<SwapchainCallback> callbacks_create_swapchain;
    std::vector<SwapchainCallback> callbacks_destroy_swapchain;

    uint32_t current_image_index = 0;
    // swapchain 处于 suboptimal：下一帧 acquire 之前重建一次
    bool swapchain_recreate_pending = false;

    result_t create_swapchain_internal() {
        if (result_t result = vkCreateSwapchainKHR(vulkan_device->get_device(), &swapchain_create_info, nullptr, &swapchain)) {
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to create a swapchain!\nError code: {}\n", int32_t(result));
            return result;
        }

        // 获取交换链图像
        uint32_t swapchain_image_count = 0;
        if (result_t result = vkGetSwapchainImagesKHR(vulkan_device->get_device(), swapchain, &swapchain_image_count, nullptr)) {
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to get the count of swapchain images!\nError code: {}\n", int32_t(result));
            return result;
        }
        swapchain_images.resize(swapchain_image_count);
        if (result_t result = vkGetSwapchainImagesKHR(vulkan_device->get_device(), swapchain, &swapchain_image_count, swapchain_images.data())) {
            outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to get swapchain images!\nError code: {}\n", int32_t(result));
            return result;
        }

        // 创建image view (图像的使用方式)
        swapchain_image_views.resize(swapchain_image_count);
        VkImageViewCreateInfo swapchain_image_view_create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain_create_info.imageFormat,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
        for (size_t i = 0; i < swapchain_image_count; i++) {
            swapchain_image_view_create_info.image = swapchain_images[i];
            if (result_t result = vkCreateImageView(vulkan_device->get_device(), &swapchain_image_view_create_info, nullptr, &swapchain_image_views[i]); result != VK_SUCCESS) {
                outstream << std::format("[ VulkanSwapchainManager ] ERROR\nFailed to create a swapchain image view!\nError code: {}\n", int32_t(result));
                return result;
            }
        }

        // 与 swapchain image 一一对应的 render-finished semaphore
        render_finished_semaphores.clear();
        render_finished_semaphores.resize(swapchain_image_count);
        return VK_SUCCESS;
    }
};
