#pragma once
#include "../Start.h"
#include "../VulkanBase/VulkanCore.h"
#include "../VulkanBase/components/VulkanSync.h"
#include "../VulkanBase/components/VulkanCommand.h"
#include "../VulkanBase/components/VulkanDescriptor.h"
#include "../UI/ImGuiManager.h"

class SharedResourceManager {
public:
    static SharedResourceManager& get_singleton() {
        static SharedResourceManager singleton = SharedResourceManager();
        return singleton;
    }

    bool initialize(GLFWwindow* window) {
        this->window = window;
        shared_fence = std::make_unique<fence>();
        semaphore_image_is_available = std::make_unique<semaphore>();
        semaphore_rendering_is_over = std::make_unique<semaphore>();

        // 创建共享命令池
        command_pool = std::make_unique<VulkanCommandPool>(
            VulkanCore::get_singleton().get_vulkan_device().get_queue_family_index_graphics(),
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
        );

        initialize_rpwf();
        return initialize_imgui_resources();
    }

    // 退出前的显式释放，必须在销毁 VkDevice 之前调用：这些对象都在析构里调用 vkDestroy*，
    // 晚于设备销毁就会变成 "vkDestroyXxx: Invalid device"。顺序：先 descriptor pool /
    // 命令池（其子对象已由 demo 与 ImGui 释放），再是信号量与 fence。
    void shutdown() {
        imgui_descriptor_pool.reset();
        command_pool.reset();
        semaphore_rendering_is_over.reset();
        semaphore_image_is_available.reset();
        shared_fence.reset();
        window = nullptr;
    }

    // Getter
    fence& get_shared_fence() { return *shared_fence; }
    semaphore& get_semaphore_image_is_available() { return *semaphore_image_is_available; }
    semaphore& get_semaphore_rendering_is_over() { return *semaphore_rendering_is_over; }
    VulkanCommandPool& get_command_pool() { return *command_pool; }
    VulkanDescriptorPool& get_imgui_descriptor_pool() { return *imgui_descriptor_pool; }
    GLFWwindow* get_window() { return window; }
    static const VulkanRenderPass& get_render_pass() { return VulkanPipelineManager::get_singleton().get_rpwf_screen().render_pass;}
    static const std::vector<VulkanFramebuffer> &get_framebuffers() { return VulkanPipelineManager::get_singleton().get_rpwf_screen().framebuffers; }

    static const VulkanRenderPass& get_render_pass_imageless_framebuffer() { return VulkanPipelineManager::get_singleton().get_rpwf_screen_imageless_framebuffer().render_pass;}
    static const VulkanFramebuffer &get_imageless_framebuffer() { return VulkanPipelineManager::get_singleton().get_rpwf_screen_imageless_framebuffer().framebuffer; }

    static const VulkanRenderPass& get_render_pass_imgui() { return VulkanPipelineManager::get_singleton().get_rpwf_imgui().render_pass;}
    static const std::vector<VulkanFramebuffer> &get_framebuffers_imgui() { return VulkanPipelineManager::get_singleton().get_rpwf_imgui().framebuffers; }

    static const VulkanRenderPass& get_render_pass_offscreen() { return VulkanPipelineManager::get_singleton().get_rpwf_offscreen().render_pass;}
    static const VulkanFramebuffer &get_framebuffers_offscreen() { return VulkanPipelineManager::get_singleton().get_rpwf_offscreen().framebuffer; }

    static const VulkanRenderPass& get_render_pass_ds() { return VulkanPipelineManager::get_singleton().get_rpwf_ds().render_pass;}
    static const std::vector<VulkanFramebuffer>& get_framebuffers_ds() { return VulkanPipelineManager::get_singleton().get_rpwf_ds().framebuffers;}

    void initialize_rpwf() {
        VulkanPipelineManager::get_singleton().create_rpwf_screen_imageless_framebuffer();
        VulkanPipelineManager::get_singleton().create_rpwf_screen();
        VulkanPipelineManager::get_singleton().create_rpwf_imgui();
        VulkanPipelineManager::get_singleton().create_rpwf_offscreen(window_size);
        VulkanPipelineManager::get_singleton().create_rpwf_ds();
        VulkanPipelineManager::get_singleton().create_rpwf_deferred_to_screen();
        VulkanPipelineManager::get_singleton().create_rpwf_offscreen_ds();
    }

private:
    GLFWwindow* window = nullptr;

    // 共享同步对象
    std::unique_ptr<fence> shared_fence;
    std::unique_ptr<semaphore> semaphore_image_is_available;
    std::unique_ptr<semaphore> semaphore_rendering_is_over;

    // 共享命令池
    std::unique_ptr<VulkanCommandPool> command_pool;

    // ImGui资源
    std::unique_ptr<VulkanDescriptorPool> imgui_descriptor_pool;

    bool initialize_imgui_resources() {
        // ImGui描述符池配置
        VkDescriptorPoolSize imgui_pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
        };

        // ImGui_ImplVulkan_Shutdown() 会 vkFreeDescriptorSets() 释放它自己的 set，
        // 因此池必须带 VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
        //（否则 validation 报 VUID-vkFreeDescriptorSets-descriptorPool-00312）。
        imgui_descriptor_pool = std::make_unique<VulkanDescriptorPool>(
            1000 * IM_ARRAYSIZE(imgui_pool_sizes),
            imgui_pool_sizes,
            VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
        );
        return true;
    }
};
