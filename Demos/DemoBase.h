#pragma once
#include "../Start.h"
#include "../VulkanBase/VulkanCore.h"
#include "../VulkanBase/VulkanSwapchainManager.h"
#include "../VulkanBase/VulkanPipelineManager.h"
#include "../VulkanBase/components/VulkanPipepline.h"
#include "../VulkanBase/components/VulkanDescriptor.h"
#include "../VulkanBase/components/VulkanShaderModule.h"
#include "../VulkanBase/components/VulkanCommand.h"

#include "DemoCategories.h"
#include "SharedResourceManager.h"


class DemoBase {
public:
    DemoBase(DemoType type, DemoCategoryType category,  const std::string& description = "")
           : scene_type(type), scene_category(category),  scene_description(description) {}

    virtual ~DemoBase() = default;

    // Virtual functions
    virtual bool initialize_scene_resources() = 0;
    virtual void cleanup_scene_resources() = 0;
    virtual void render_frame() = 0;

    void show_demo_basic_info() {
        if (ImGui::Begin("Basic info: ")) {
            ImGui::Text("current demo: %s", get_type().c_str());
            ImGui::Text("description: %s", get_description().c_str());

        }
        ImGui::End();
    }

    virtual void draw_custom_ui() {

    }


    // Getter
    DemoType get_type() const { return scene_type; }
    DemoCategoryType get_category() const { return scene_category; }
    const std::string& get_description() const { return scene_description; }
    VkCommandBuffer get_command_buffer() const { return command_buffer; }

    bool show_demo_window = true;
    [[nodiscard]] bool get_show_demo_window() const {
        return show_demo_window;
    }

    virtual void update(float frame_timer_from_manager){}

    // Setter
    void set_window(GLFWwindow *window) { this->window = window; }

protected:
    GLFWwindow *window;
    DemoType scene_type;
    DemoCategoryType scene_category;
    std::string scene_description;

    // vulkan pipeline
    VulkanPipeline pipeline;
    VulkanPipelineLayout pipeline_layout;

    // vulkan descriptor set layout
    VulkanDescriptorSetLayout descriptor_set_layout;

    // vulkan command buffer
    VulkanCommandBuffer command_buffer;

    const RenderPassWithFramebuffers& imgui_rpwf = VulkanPipelineManager::get_singleton().get_rpwf_imgui();

    static const auto& get_shared_render_pass() {
        return VulkanPipelineManager::get_singleton().get_rpwf_screen().render_pass;
    }

    static const auto& get_shared_render_pass_imageless_framebuffer() {
        return VulkanPipelineManager::get_singleton().get_rpwf_screen_imageless_framebuffer().render_pass;
    }

    static const auto& get_shared_render_pass_offscreen() {
        return VulkanPipelineManager::get_singleton().get_rpwf_offscreen().render_pass;
    }

    bool allocate_command_buffer() {
        return SharedResourceManager::get_singleton().get_command_pool().allocate_buffers(command_buffer);
    }

    void free_command_buffer() {
        SharedResourceManager::get_singleton().get_command_pool().free_buffers(command_buffer);
    }

    void imgui_render(uint32_t i, array_ref<const VkClearValue>clear_values) {
        const auto &[imgui_render_pass, imgui_framebuffers] = imgui_rpwf;
        imgui_render_pass.cmd_begin(command_buffer, imgui_framebuffers[i],
                {{}, window_size}, clear_values);
        ImGuiManager::get_singleton().render(command_buffer);
        imgui_render_pass.cmd_end(command_buffer);
    }



};
