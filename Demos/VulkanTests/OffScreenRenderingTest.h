#pragma once
#include "../DemoBase.h"
#include "../../Geometry/Vertex.h"
#include "../../Start.h"

#include "../../VulkanBase/components/VulkanTexture.h"
#include "../../VulkanBase/components/VulkanSampler.h"
#include "../../VulkanBase/components/VulkanMemory.h"

class OffScreenRenderingTest : public DemoBase {
public:
    OffScreenRenderingTest()
    : DemoBase("OffScreenRenderingTest", DemoCategoryType::VULKAN_TESTS, ""),
      push_constants_offscreen({
          { static_cast<float>(default_window_size.width), static_cast<float>(default_window_size.height) },
          { { 0.f, 0.f }, { 0.f, 0.f } }
      })
    {}
    ~OffScreenRenderingTest() override = default;

    bool initialize_scene_resources() override {
        allocate_command_buffer();
        if (!create_pipeline_layout() || !create_pipeline()
            || !create_pipeline_layout_offscreen() || !create_pipeline_offscreen()) {
            return false;
        }

        VkSamplerCreateInfo sampler_create_info = VulkanTexture2D::get_sampler_create_info();
        sampler = std::make_unique<VulkanSampler>(sampler_create_info);

        if (!create_descriptor_resources()) {
            return false;
        }

        return true;
    }

    void cleanup_scene_resources() override {
        // SharedResourceManager::get_singleton().get_shared_fence().wait_and_reset();
        // 清理资源
        descriptor_set.reset();
        descriptor_pool.reset();
        sampler.reset();
        vertex_buffer.reset();

        // 清理管线
        pipeline.~VulkanPipeline();
        pipeline_layout.~VulkanPipelineLayout();
        descriptor_set_layout.~VulkanDescriptorSetLayout();

        pipeline_line.~VulkanPipeline();
        pipeline_layout_line.~VulkanPipelineLayout();

        free_command_buffer();
    }

    void render_frame() override {
        const auto& [render_pass, framebuffers] = VulkanPipelineManager::get_singleton().get_rpwf_screen();
        const auto& [offscreen_render_pass, offscreen_framebuffer] = VulkanPipelineManager::get_singleton().get_rpwf_offscreen();

        auto current_image_index = VulkanSwapchainManager::get_singleton().get_current_image_index();

        VkClearValue clear_color = { .color = { 1.f, 1.f, 1.f, 1.f } };


        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        {
            if (clear_canvas) {
                VulkanPipelineManager::get_singleton().cmd_clear_canvas(command_buffer,VkClearColorValue{});
                clear_canvas = false;
            }

            // 离屏部分rpwf
            offscreen_render_pass.cmd_begin(command_buffer, offscreen_framebuffer, {{}, window_size});
            {
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_line);
                vkCmdPushConstants(command_buffer, pipeline_layout_line, VK_SHADER_STAGE_VERTEX_BIT, 0, 24, &push_constants_offscreen);
                vkCmdDraw(command_buffer, 2, 1, 0, 0);
            }
            offscreen_render_pass.cmd_end(command_buffer);

            // 屏幕部分rpwf
            render_pass.cmd_begin(command_buffer, framebuffers[current_image_index],
                                       {{}, window_size}, clear_color);
            {
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

                //VkExtent2D底层是两个uint32_t，得转为float
                auto& swapchain_info = VulkanSwapchainManager::get_singleton().get_swapchain_create_info();
                glm::vec2 windowSize = { static_cast<float>(swapchain_info.imageExtent.width), static_cast<float>(swapchain_info.imageExtent.height) };
                vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, &windowSize);
                // 更新viewportSize以匹配当前窗口大小
                push_constants_offscreen.viewportSize = windowSize;
                // 使用当前的viewportSize
                vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 8, 8, &push_constants_offscreen.viewportSize);

                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, descriptor_set->Address(), 0, nullptr);
                vkCmdDraw(command_buffer, 4, 1, 0, 0);
            }
            render_pass.cmd_end(command_buffer);

            // imgui rpwf
            imgui_render(current_image_index,clear_color);
        }
        command_buffer.end();
        glfwGetCursorPos(window, &mouseX, &mouseY);
        push_constants_offscreen.offsets[canvas_index = !canvas_index] = { mouseX, mouseY };
        clear_canvas = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
    }


private:
    std::unique_ptr<VulkanVertexBuffer> vertex_buffer;
    std::unique_ptr<VulkanTexture2D> texture_image;
    std::unique_ptr<VulkanSampler> sampler;
    std::unique_ptr<VulkanDescriptorPool> descriptor_pool;
    std::unique_ptr<VulkanDescriptorSet> descriptor_set;

    // offscreen pipeline
    VulkanPipelineLayout pipeline_layout_line;
    VulkanPipeline pipeline_line;

    bool clear_canvas = true, canvas_index = 0;
    double mouseX, mouseY;

    // Push constants for offscreen rendering
    struct {
        glm::vec2 viewportSize;
        glm::vec2 offsets[2];
    } push_constants_offscreen;

    bool create_pipeline_layout_offscreen() {
        VkPushConstantRange push_constant_range_offscreen = { VK_SHADER_STAGE_VERTEX_BIT, 0, 24 };
        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range_offscreen,
        };
        return (pipeline_layout_line.create(pipeline_layout_create_info) == VK_SUCCESS);
    }

    bool create_pipeline_layout() {
        VkDescriptorSetLayoutBinding descriptor_set_layout_binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        };
        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
            .bindingCount = 1,
            .pBindings = &descriptor_set_layout_binding
        };
        descriptor_set_layout.create(descriptor_set_layout_create_info);
        VkPushConstantRange push_constant_ranges_screen[] = {
            { VK_SHADER_STAGE_VERTEX_BIT, 0, 16 },
            { VK_SHADER_STAGE_FRAGMENT_BIT, 8, 8 }
        };
        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
            .pushConstantRangeCount = 2,
            .pPushConstantRanges = push_constant_ranges_screen,
        };
        pipeline_layout_create_info.setLayoutCount = 1;
        pipeline_layout_create_info.pSetLayouts = descriptor_set_layout.Address();
        return pipeline_layout.create(pipeline_layout_create_info) == VK_SUCCESS;
    }

    bool create_pipeline_offscreen() {
        VulkanShaderModule vert_offscreen(get_shader_path("VulkanTests/Line.vert.spv").string().c_str());
        VulkanShaderModule frag_offscreen(get_shader_path("VulkanTests/Line.frag.spv").string().c_str());
        VkPipelineShaderStageCreateInfo shader_stage_create_info[2] = {
            vert_offscreen.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag_offscreen.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        GraphicsPipelineCreateInfoPack pipeline_create_info_pack;
        pipeline_create_info_pack.create_info.layout = pipeline_layout_line;
        pipeline_create_info_pack.create_info.renderPass = get_shared_render_pass_offscreen();
        pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        pipeline_create_info_pack.viewports.emplace_back(0.f, 0.f, float(window_size.width), float(window_size.height), 0.f, 1.f);
        pipeline_create_info_pack.scissors.emplace_back(VkOffset2D{},window_size);
        pipeline_create_info_pack.rasterization_state_create_info.lineWidth = 1.f;
        pipeline_create_info_pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        pipeline_create_info_pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
        pipeline_create_info_pack.update_all_arrays();
        pipeline_create_info_pack.create_info.stageCount = 2;
        pipeline_create_info_pack.create_info.pStages = shader_stage_create_info;
        return pipeline_line.create(pipeline_create_info_pack) == VK_SUCCESS;
    }

    bool create_pipeline() {
        VulkanShaderModule vert(get_shader_path("VulkanTests/CanvasToScreen.vert.spv").string().c_str());
        VulkanShaderModule frag(get_shader_path("VulkanTests/CanvasToScreen.frag.spv").string().c_str());
        static VkPipelineShaderStageCreateInfo shader_stage_create_infos_screen[2] = {
            vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        auto create = [&] {
            if (current_demo_name != "OffScreenRenderingTest") return false;
            GraphicsPipelineCreateInfoPack pipeline_create_info_pack;
            pipeline_create_info_pack.create_info.layout = pipeline_layout;
            pipeline_create_info_pack.create_info.renderPass = get_shared_render_pass();
            pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            pipeline_create_info_pack.viewports.emplace_back(0.f, 0.f, float(window_size.width), float(window_size.height), 0.f, 1.f);
            pipeline_create_info_pack.scissors.emplace_back(VkOffset2D{},window_size);
            pipeline_create_info_pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            pipeline_create_info_pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
            pipeline_create_info_pack.update_all_arrays();
            pipeline_create_info_pack.create_info.stageCount = 2;
            pipeline_create_info_pack.create_info.pStages = shader_stage_create_infos_screen;

            return pipeline.create(pipeline_create_info_pack) == VK_SUCCESS;

        };
        auto destroy = [this] {
            if (current_demo_name != "OffScreenRenderingTest") return;
            pipeline.~VulkanPipeline();
        };
        add_swapchain_create_callback(create);
        add_swapchain_destroy_callback(destroy);
        return create();
    }

    bool create_descriptor_resources() {
        // 创建描述符池
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
        };

        descriptor_pool = std::make_unique<VulkanDescriptorPool>(1, pool_sizes);

        // 分配描述符集
        descriptor_set = std::make_unique<VulkanDescriptorSet>();
        descriptor_pool->allocate_sets(*descriptor_set, descriptor_set_layout);

        // 写入描述符
        descriptor_set->write(VulkanPipelineManager::get_singleton().get_ca_canvas().get_descriptor_image_info(*sampler), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        return true;
    }
};
