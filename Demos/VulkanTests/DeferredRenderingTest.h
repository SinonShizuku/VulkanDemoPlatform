#pragma once
#include "../DemoBase.h"
#include "../../Geometry/Vertex.h"

#include "../../VulkanBase/components/VulkanTexture.h"
#include "../../VulkanBase/components/VulkanMemory.h"

class DeferredRenderingTest : public DemoBase {
public:
    DeferredRenderingTest()
    : DemoBase("DeferredRenderingTest", DemoCategoryType::VULKAN_TESTS, "")
    {}
    ~DeferredRenderingTest() override = default;

    bool initialize_scene_resources() override {
        allocate_command_buffer();
        if (!create_pipeline_layout() || !create_pipeline()) {
            return false;
        }

        vertex3D_1 vertices[] = {
            //x+
            { {  1,  1, -1 }, {  1,  0,  0 }, { 1, 1, 1, 1 } },
            { {  1, -1, -1 }, {  1,  0,  0 }, { 1, 1, 1, 1 } },
            { {  1,  1,  1 }, {  1,  0,  0 }, { 1, 1, 1, 1 } },
            { {  1, -1,  1 }, {  1,  0,  0 }, { 1, 1, 1, 1 } },
            //x-
            { { -1,  1,  1 }, { -1,  0,  0 }, { 1, 1, 1, 1 } },
            { { -1, -1,  1 }, { -1,  0,  0 }, { 1, 1, 1, 1 } },
            { { -1,  1, -1 }, { -1,  0,  0 }, { 1, 1, 1, 1 } },
            { { -1, -1, -1 }, { -1,  0,  0 }, { 1, 1, 1, 1 } },
            //y+
            { {  1,  1, -1 }, {  0,  1,  0 }, { 1, 1, 1, 1 } },
            { {  1,  1,  1 }, {  0,  1,  0 }, { 1, 1, 1, 1 } },
            { { -1,  1, -1 }, {  0,  1,  0 }, { 1, 1, 1, 1 } },
            { { -1,  1,  1 }, {  0,  1,  0 }, { 1, 1, 1, 1 } },
            //y-
            { {  1, -1, -1 }, {  0, -1,  0 }, { 1, 1, 1, 1 } },
            { { -1, -1, -1 }, {  0, -1,  0 }, { 1, 1, 1, 1 } },
            { {  1, -1,  1 }, {  0, -1,  0 }, { 1, 1, 1, 1 } },
            { { -1, -1,  1 }, {  0, -1,  0 }, { 1, 1, 1, 1 } },
            //z+
            { {  1,  1,  1 }, {  0,  0,  1 }, { 1, 1, 1, 1 } },
            { {  1, -1,  1 }, {  0,  0,  1 }, { 1, 1, 1, 1 } },
            { { -1,  1,  1 }, {  0,  0,  1 }, { 1, 1, 1, 1 } },
            { { -1, -1,  1 }, {  0,  0,  1 }, { 1, 1, 1, 1 } },
            //z-
            { { -1,  1, -1 }, {  0,  0, -1 }, { 1, 1, 1, 1 } },
            { { -1, -1, -1 }, {  0,  0, -1 }, { 1, 1, 1, 1 } },
            { {  1,  1, -1 }, {  0,  0, -1 }, { 1, 1, 1, 1 } },
            { {  1, -1, -1 }, {  0,  0, -1 }, { 1, 1, 1, 1 } }
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

        if (!create_descriptor_resources()) {
            return false;
        }

        return true;
    }

    bool create_descriptor_resources() {
        struct {
            glm::mat4 proj = flip_vertical(glm::infinitePerspectiveLH_ZO(glm::radians(60.f), float(window_size.width) / window_size.height, 0.1f));
            glm::mat4 view = glm::lookAtLH(glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), glm::vec3(-1, 0, 0));
            int32_t light_count;
            struct {
                alignas(16) glm::vec3 position; //光源位置
                alignas(16) glm::vec3 color;    //光的颜色
                float strength;                 //光的强度
            } lights[8];
        } descriptor_constants;

        descriptor_constants.light_count = 3;
        descriptor_constants.lights[0] = { { 0.f,  4.f,  6.f }, { 1.f, 0.f, 0.f }, 100.f }; //红光，光源在离观察者最近的两个立方体的正上方
        descriptor_constants.lights[1] = { { 0.f,  0.f, 16.f }, { 0.f, 1.f, 0.f }, 100.f }; //绿光，光源在z轴上这堆立方体的中央位置
        descriptor_constants.lights[2] = { { 0.f, -4.f,  6.f }, { 0.f, 0.f, 1.f }, 100.f }; //蓝光，光源在离观察者最近的两个立方体的中间
        uniform_buffer = std::make_unique<VulkanUniformBuffer>(sizeof(descriptor_constants));
        uniform_buffer->transfer_data(descriptor_constants);

        VkDescriptorBufferInfo buffer_infos[] = {
            { *uniform_buffer, 0, sizeof(glm::mat4) * 2 },
            { *uniform_buffer, 0, VK_WHOLE_SIZE}
        };

        VkDescriptorPoolSize descriptor_pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 2 }
        };
        descriptor_pool = std::make_unique<VulkanDescriptorPool>(2,descriptor_pool_sizes);
        descriptor_set_gbuffer = std::make_unique<VulkanDescriptorSet>();
        descriptor_set_composition = std::make_unique<VulkanDescriptorSet>();

        descriptor_pool->allocate_sets(*descriptor_set_gbuffer, *descriptor_set_layout_gbuffer);
        descriptor_pool->allocate_sets(*descriptor_set_composition, *descriptor_set_layout_composition);

        descriptor_set_gbuffer->write(buffer_infos[0], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0,0);
        descriptor_set_composition->write(buffer_infos[1], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0,0);

        auto update_descriptor_set_input_attachments = [this] {
            if (current_demo_name != this->get_type()) return;
            VkDescriptorImageInfo image_infos [2] = {
                {VK_NULL_HANDLE, VulkanPipelineManager::get_singleton().get_ca_deferred_to_screen_normal_z().get_image_view(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                {VK_NULL_HANDLE, VulkanPipelineManager::get_singleton().get_ca_deferred_to_screen_albedo_specular().get_image_view(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL  }
            };
            descriptor_set_composition->write(image_infos, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1,0);
        };
        VulkanSwapchainManager::get_singleton().add_callback_create_swapchain(update_descriptor_set_input_attachments);
        update_descriptor_set_input_attachments();

        return true;
    }

    void cleanup_scene_resources() override {
        vertex_buffer_pervertex.reset();
        vertex_buffer_perinstance.reset();
        descriptor_pool.reset();
        descriptor_set_layout_gbuffer.reset();
        descriptor_set_layout_composition.reset();
        descriptor_set_gbuffer.reset();
        descriptor_set_composition.reset();

        // 清理管线
        pipeline.~VulkanPipeline();
        pipeline_gbuffer.~VulkanPipeline();
        pipeline_layout.~VulkanPipelineLayout();
        pipeline_layout_gbuffer.~VulkanPipelineLayout();
        descriptor_set_layout.~VulkanDescriptorSetLayout();

        free_command_buffer();
    }

    void render_frame() override {
        const auto& [render_pass, framebuffers] = VulkanPipelineManager::get_singleton().get_rpwf_deferred_to_screen();
        auto current_image_index = VulkanSwapchainManager::get_singleton().get_current_image_index();

        VkClearValue clear_values[4] = {
            { .color = {  } },
            { .color = {  } },
            { .color = {  } },
            { .depthStencil = { 1.f, 0 } }
        };

        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        {
            // 屏幕部分rpwf
            render_pass.cmd_begin(command_buffer, framebuffers[current_image_index],
                                       {{}, window_size}, clear_values);
            {
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_gbuffer);
                VkBuffer buffers[2] = {*vertex_buffer_pervertex, *vertex_buffer_perinstance};
                VkDeviceSize offsets[2] = {};
                vkCmdBindVertexBuffers(command_buffer, 0, 2, buffers, offsets);
                vkCmdBindIndexBuffer(command_buffer, *index_buffer, 0, VK_INDEX_TYPE_UINT16);
                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_gbuffer, 0, 1, descriptor_set_gbuffer->Address(), 0, nullptr);
                // draw
                vkCmdDrawIndexed(command_buffer, 36, 12, 0, 0, 0);
            }
            render_pass.cmd_next(command_buffer);
            {
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, descriptor_set_composition->Address(), 0, nullptr);
                vkCmdDraw(command_buffer, 4, 1, 0, 0);
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
    std::unique_ptr<VulkanDescriptorPool> descriptor_pool;
    std::unique_ptr<VulkanDescriptorSetLayout> descriptor_set_layout_gbuffer;
    std::unique_ptr<VulkanDescriptorSetLayout> descriptor_set_layout_composition;
    std::unique_ptr<VulkanDescriptorSet> descriptor_set_gbuffer;
    std::unique_ptr<VulkanDescriptorSet> descriptor_set_composition;
    VulkanPipelineLayout pipeline_layout_gbuffer;
    VulkanPipeline pipeline_gbuffer;

    std::unique_ptr<VulkanUniformBuffer> uniform_buffer;

    bool create_pipeline_layout() {
        descriptor_set_layout_gbuffer = std::make_unique<VulkanDescriptorSetLayout>();
        descriptor_set_layout_composition = std::make_unique<VulkanDescriptorSetLayout>();
        // G-buffer
        VkDescriptorSetLayoutBinding descriptor_set_layout_binding_gbuffer = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT};
        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
            .bindingCount = 1,
            .pBindings = &descriptor_set_layout_binding_gbuffer
        };
        descriptor_set_layout_gbuffer->create(descriptor_set_layout_create_info);
        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
            .setLayoutCount = 1,
            .pSetLayouts = descriptor_set_layout_gbuffer->Address()
        };
        pipeline_layout_gbuffer.create(pipeline_layout_create_info);

        // composition
        VkDescriptorSetLayoutBinding descriptor_set_layout_bindings_composition[2] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT},
            {1, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 2, VK_SHADER_STAGE_FRAGMENT_BIT}
        };
        descriptor_set_layout_create_info.bindingCount = 2;
        descriptor_set_layout_create_info.pBindings = descriptor_set_layout_bindings_composition;
        descriptor_set_layout_composition->create(descriptor_set_layout_create_info);
        pipeline_layout_create_info.pSetLayouts = descriptor_set_layout_composition->Address();
        return pipeline_layout.create(pipeline_layout_create_info) == VK_SUCCESS;
    }

    bool create_pipeline() {
        static VulkanShaderModule vert_gbuffer(get_shader_path("VulkanTests/GBuffer.vert.spv").string().c_str());
        static VulkanShaderModule frag_gbuffer(get_shader_path("VulkanTests/GBuffer.frag.spv").string().c_str());
        static VkPipelineShaderStageCreateInfo shader_stage_create_infos_gbuffer[2] = {
            vert_gbuffer.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag_gbuffer.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        static VulkanShaderModule vert_composition(get_shader_path("VulkanTests/Composition.vert.spv").string().c_str());
        static VulkanShaderModule frag_composition(get_shader_path("VulkanTests/Composition.frag.spv").string().c_str());
        static VkPipelineShaderStageCreateInfo shader_stage_create_infos_composition[2] = {
            vert_composition.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag_composition.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        static constexpr int32_t shininess = 64;
        static VkSpecializationMapEntry map_entry = {
            1,                //constantID，被特化常量在着色器中的的ID
            0,                //offset，特化数据在VkSpecializationInfo::pData中的起始位置
            sizeof shininess  //size，特化数据的大小，单位为字节
        };
        static VkSpecializationInfo specialization_info = {
            1,                //mapEntryCount
            &map_entry,        //pMapEntries
            sizeof shininess, //dataSize
            &shininess        //pData
        };
        auto create = [&] {
            if (current_demo_name != "DeferredRenderingTest") return false;
            GraphicsPipelineCreateInfoPack pipeline_create_info_pack;
            pipeline_create_info_pack.create_info.layout = pipeline_layout_gbuffer;
            pipeline_create_info_pack.create_info.renderPass = VulkanPipelineManager::get_singleton().get_rpwf_deferred_to_screen().render_pass;
            pipeline_create_info_pack.create_info.subpass = 0;

            pipeline_create_info_pack.vertex_input_bindings.emplace_back(0, sizeof(vertex3D_1), VK_VERTEX_INPUT_RATE_VERTEX);
            pipeline_create_info_pack.vertex_input_bindings.emplace_back(1,sizeof(glm::vec3),VK_VERTEX_INPUT_RATE_INSTANCE);
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex3D_1, position));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(vertex3D_1, normal));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vertex3D_1, albedo_specular));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(3, 1, VK_FORMAT_R32G32B32_SFLOAT, 0);
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

            pipeline_create_info_pack.color_blend_attachment_states.resize(2);
            pipeline_create_info_pack.color_blend_attachment_states[0].colorWriteMask = 0b1111;
            pipeline_create_info_pack.color_blend_attachment_states[1].colorWriteMask = 0b1111;
            pipeline_create_info_pack.update_all_arrays();
            pipeline_create_info_pack.create_info.stageCount = 2;
            pipeline_create_info_pack.create_info.pStages = shader_stage_create_infos_gbuffer;
            pipeline_gbuffer.create(pipeline_create_info_pack);

            // composition
            pipeline_create_info_pack.create_info.layout = pipeline_layout;
            pipeline_create_info_pack.create_info.subpass = 1;
            pipeline_create_info_pack.create_info.pStages = shader_stage_create_infos_composition;
            pipeline_create_info_pack.vertex_input_state_create_info.vertexBindingDescriptionCount = 0;
            pipeline_create_info_pack.vertex_input_state_create_info.vertexAttributeDescriptionCount = 0;
            pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
            pipeline_create_info_pack.color_blend_state_create_info.attachmentCount = 1;
            return pipeline.create(pipeline_create_info_pack) == VK_SUCCESS;
        };
        auto destroy = [this] {
            if (current_demo_name != "DeferredRenderingTest") return;
            pipeline.~VulkanPipeline();
            pipeline_gbuffer.~VulkanPipeline();
        };
        VulkanSwapchainManager::get_singleton().add_callback_create_swapchain(create);
        VulkanSwapchainManager::get_singleton().add_callback_destroy_swapchain(destroy);
        return create();
    }


};
