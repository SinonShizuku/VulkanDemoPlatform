#pragma once
#include "../DemoBase.h"
#include "../../Geometry/Vertex.h"

#include "../../VulkanBase/components/VulkanTexture.h"
#include "../../VulkanBase/components/VulkanMemory.h"


class DepthAttachmentTest : public DemoBase {
public:
    DepthAttachmentTest()
    : DemoBase("DepthAttachmentTest", DemoCategoryType::VULKAN_TESTS, "")
    {}
    ~DepthAttachmentTest() override = default;

    bool initialize_scene_resources() override {
        allocate_command_buffer();
        if (!create_pipeline_layout() || !create_pipeline()) {
            return false;
        }

        vertex3D vertices[] = {
            //x+
            { {  1,  1, -1 }, {0,0,0},{0,0},{ 1, 0, 0, 1 } },
            { {  1, -1, -1 }, {0,0,0},{0,0},{ 1, 0, 0, 1 } },
            { {  1,  1,  1 }, {0,0,0},{0,0},{ 1, 0, 0, 1 } },
            { {  1, -1,  1 }, {0,0,0},{0,0},{ 1, 0, 0, 1 } },
            //x-
            { { -1,  1,  1 }, {0,0,0},{0,0},{ 0, 1, 1, 1 } },
            { { -1, -1,  1 }, {0,0,0},{0,0},{ 0, 1, 1, 1 } },
            { { -1,  1, -1 }, {0,0,0},{0,0},{ 0, 1, 1, 1 } },
            { { -1, -1, -1 }, {0,0,0},{0,0},{ 0, 1, 1, 1 } },
            //y+
            { {  1,  1, -1 }, {0,0,0},{0,0},{ 0, 1, 0, 1 } },
            { {  1,  1,  1 }, {0,0,0},{0,0},{ 0, 1, 0, 1 } },
            { { -1,  1, -1 }, {0,0,0},{0,0},{ 0, 1, 0, 1 } },
            { { -1,  1,  1 }, {0,0,0},{0,0},{ 0, 1, 0, 1 } },
            //y-
            { {  1, -1, -1 }, {0,0,0},{0,0},{ 1, 0, 1, 1 } },
            { { -1, -1, -1 }, {0,0,0},{0,0},{ 1, 0, 1, 1 } },
            { {  1, -1,  1 }, {0,0,0},{0,0},{ 1, 0, 1, 1 } },
            { { -1, -1,  1 }, {0,0,0},{0,0},{ 1, 0, 1, 1 } },
            //z+
            { {  1,  1,  1 }, {0,0,0},{0,0},{ 0, 0, 1, 1 } },
            { {  1, -1,  1 }, {0,0,0},{0,0},{ 0, 0, 1, 1 } },
            { { -1,  1,  1 }, {0,0,0},{0,0},{ 0, 0, 1, 1 } },
            { { -1, -1,  1 }, {0,0,0},{0,0},{ 0, 0, 1, 1 } },
            //z-
            { { -1,  1, -1 }, {0,0,0},{0,0},{ 1, 1, 0, 1 } },
            { { -1, -1, -1 }, {0,0,0},{0,0},{ 1, 1, 0, 1 } },
            { {  1,  1, -1 }, {0,0,0},{0,0},{ 1, 1, 0, 1 } },
            { {  1, -1, -1 }, {0,0,0},{0,0},{ 1, 1, 0, 1 } }
        };
        vertex_buffer_pervertex = std::make_unique<VulkanVertexBuffer>(sizeof(vertices));
        vertex_buffer_pervertex->transfer_data(vertices);

        glm::vec3 offsets[] = {
            { -4, -4,  6 }, {  4, -4,  6 },
            { -4,  4, 10 }, {  4,  4, 10 },
            { -4, -4, 14 }, {  4, -4, 14 },
            { -4,  4, 18 }, {  4,  4, 18 },
            { -4, -4, 22 }, {  4, -4, 22 },
            { -4,  4, 26 }, {  4,  4, 26 }
        };
        vertex_buffer_perinstance = std::make_unique<VulkanVertexBuffer>(sizeof(offsets));
        vertex_buffer_perinstance->transfer_data(offsets);

        uint16_t indices[36] = { 0, 1, 2, 2, 1, 3 };
        for (size_t i = 1; i < 6; i++)
            for (size_t j = 0; j < 6; j++)
                indices[i * 6 + j] = indices[j] + i * 4;
        index_buffer = std::make_unique<VulkanIndexBuffer>(sizeof(indices));
        index_buffer->transfer_data(indices);

        return true;
    }

    void cleanup_scene_resources() override {
        vertex_buffer_pervertex.reset();
        vertex_buffer_perinstance.reset();
        index_buffer.reset();

        // 清理管线
        pipeline.~VulkanPipeline();
        pipeline_layout.~VulkanPipelineLayout();
        descriptor_set_layout.~VulkanDescriptorSetLayout();

        free_command_buffer();
    }

    void render_frame() override {
        const auto& [render_pass, framebuffers] = VulkanPipelineManager::get_singleton().get_rpwf_ds();
        auto current_image_index = VulkanSwapchainManager::get_singleton().get_current_image_index();

        // Use a conventional perspective projection without flipping Y axis
        glm::mat4 proj = flip_vertical(glm::infinitePerspectiveLH_ZO(glm::radians(60.f), float(window_size.width) / window_size.height, 5.f));
        VkClearValue clear_values[2] = {
            {.color = { 0.f, 0.f, 0.f, 1.f }},
            {.depthStencil = { 1.f, 0 }}
        };

        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        {
            // 屏幕部分rpwf
            render_pass.cmd_begin(command_buffer, framebuffers[current_image_index],
                                       {{}, window_size}, clear_values);
            {
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                VkBuffer buffers[2] = {*vertex_buffer_pervertex, *vertex_buffer_perinstance};
                VkDeviceSize offsets[2] = {};
                vkCmdBindVertexBuffers(command_buffer, 0, 2, buffers, offsets);
                vkCmdBindIndexBuffer(command_buffer, *index_buffer, 0, VK_INDEX_TYPE_UINT16);
                vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 64, &proj);
                // draw
                vkCmdDrawIndexed(command_buffer, 36, 12, 0, 0, 0);
            }
            render_pass.cmd_end(command_buffer);

            // imgui rpwf
            imgui_render(current_image_index,clear_values);
        }
        command_buffer.end();
    }


private:
    std::unique_ptr<VulkanVertexBuffer> vertex_buffer_pervertex;
    std::unique_ptr<VulkanVertexBuffer> vertex_buffer_perinstance;
    std::unique_ptr<VulkanIndexBuffer> index_buffer;

    bool create_pipeline_layout() {
        VkPushConstantRange push_constant_range = {VK_SHADER_STAGE_VERTEX_BIT, 0, 64};
        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range
        };
        return pipeline_layout.create(pipeline_layout_create_info) == VK_SUCCESS;
    }

    bool create_pipeline() {
        // VulkanShaderModule vert("Shader/Into3D.vert.spv");
        // VulkanShaderModule frag("Shader/Into3d_visualizeDepth.frag.spv");
        f_compile_glsl_to_spv f_compile;
        static VulkanShaderModule vert = create_shader_module_from_glsl(f_compile, get_shader_path("VulkanTests/Into3D.vert.shader").string().c_str());
        static VulkanShaderModule frag = create_shader_module_from_glsl(f_compile, get_shader_path("VulkanTests/Into3D.frag.shader").string().c_str());;
        static VkPipelineShaderStageCreateInfo shader_stage_create_infos[2] = {
            vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        auto create = [&] {
            if (current_demo_name != "DepthAttachmentTest") return false;
            static GraphicsPipelineCreateInfoPack pipeline_create_info_pack;
            pipeline_create_info_pack.create_info.layout = pipeline_layout;
            pipeline_create_info_pack.create_info.renderPass = VulkanPipelineManager::get_singleton().get_rpwf_ds().render_pass;

            // vertex buffer
            pipeline_create_info_pack.vertex_input_bindings.emplace_back(0, sizeof(vertex3D), VK_VERTEX_INPUT_RATE_VERTEX);
            pipeline_create_info_pack.vertex_input_bindings.emplace_back(1, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_INSTANCE);
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex3D, position));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vertex3D, color));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(2, 1, VK_FORMAT_R32G32B32_SFLOAT, 0);

            pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            pipeline_create_info_pack.viewports.emplace_back(0.f, 0.f, float(window_size.width), float(window_size.height), 0.f, 1.f);
            pipeline_create_info_pack.scissors.emplace_back(VkOffset2D{},window_size);

            // 背面剔除
            pipeline_create_info_pack.rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
            pipeline_create_info_pack.rasterization_state_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

            pipeline_create_info_pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            // 深度测试
            pipeline_create_info_pack.depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
            pipeline_create_info_pack.depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
            pipeline_create_info_pack.depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS;

            pipeline_create_info_pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
            pipeline_create_info_pack.update_all_arrays();
            pipeline_create_info_pack.create_info.stageCount = 2;
            pipeline_create_info_pack.create_info.pStages = shader_stage_create_infos;

            return pipeline.create(pipeline_create_info_pack) == VK_SUCCESS;
        };
        auto destroy = [this] {
            if (current_demo_name != "DepthAttachmentTest") return;
            pipeline.~VulkanPipeline();
        };
        add_swapchain_create_callback(create);
        add_swapchain_destroy_callback(destroy);
        return create();
    }


};
