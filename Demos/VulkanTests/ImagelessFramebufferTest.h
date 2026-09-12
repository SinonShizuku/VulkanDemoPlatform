#pragma once
#include "../DemoBase.h"
#include "../../Geometry/Vertex.h"

#include "../../VulkanBase/components/VulkanTexture.h"
#include "../../VulkanBase/components/VulkanSampler.h"
#include "../../VulkanBase/components/VulkanMemory.h"


class ImagelessFramebufferTest : public DemoBase {
public:
    ImagelessFramebufferTest()
    : DemoBase("ImagelessFramebufferTest", DemoCategoryType::VULKAN_TESTS, "")
    {}
    ~ImagelessFramebufferTest() override = default;

    bool initialize_scene_resources() override {
        allocate_command_buffer();
        if (!create_pipeline_layout() || !create_pipeline()) {
            return false;
        }



        texture_vertex vertices[] = {
            { { -1.f, -1.f }, { 0, 0 } },
            { {  1.f, -1.f }, { 1, 0 } },
            { { -1.f,  1.f }, { 0, 1 } },
            { {  1.f,  1.f }, { 1, 1 } }
        };
        vertex_buffer = std::make_unique<VulkanVertexBuffer>(sizeof(vertices));
        vertex_buffer->transfer_data(vertices);

        auto page_image_root = G_PROJECT_ROOT / "Assets/pages/ImagelessFramebuffer.png";
        texture_image = std::make_unique<VulkanTexture2D>(
            page_image_root.string().c_str(),
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R8G8B8A8_UNORM,
            true
        );
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
        texture_image.reset();
        vertex_buffer.reset();

        // 清理管线
        pipeline.~VulkanPipeline();
        pipeline_layout.~VulkanPipelineLayout();
        descriptor_set_layout.~VulkanDescriptorSetLayout();

        free_command_buffer();
    }

    void render_frame() override {
        const auto& [render_pass, imageless_framebuffer] = VulkanPipelineManager::get_singleton().get_rpwf_screen_imageless_framebuffer();
        auto current_image_index = VulkanSwapchainManager::get_singleton().get_current_image_index();

        VkClearValue clear_color = { .color = { 1.f, 1.f, 1.f, 1.f } };

        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        {
            VkImageView attachment = VulkanSwapchainManager::get_singleton().get_swapchain_image_view(current_image_index);
            VkRenderPassAttachmentBeginInfo attachment_begin_info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO,
                .attachmentCount = 1,
                .pAttachments = &attachment
            };
            VkRenderPassBeginInfo render_pass_begin_info = {
                .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                .pNext = &attachment_begin_info,
                .framebuffer = imageless_framebuffer,
                .renderArea = {{}, window_size},
                .clearValueCount = 1,
                .pClearValues = &clear_color
            };
            render_pass.cmd_begin(command_buffer, render_pass_begin_info);
            {
                // 绑定资源
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffer->Address(), &offset);
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline_layout, 0, 1, descriptor_set->Address(), 0, nullptr);

                // 绘制
                vkCmdDraw(command_buffer, 4, 1, 0, 0);
            }
            render_pass.cmd_end(command_buffer);

            // imgui rpwf
            imgui_render(current_image_index,clear_color);
        }
        command_buffer.end();
    }



private:
    std::unique_ptr<VulkanVertexBuffer> vertex_buffer;
    std::unique_ptr<VulkanTexture2D> texture_image;
    std::unique_ptr<VulkanSampler> sampler;
    std::unique_ptr<VulkanDescriptorPool> descriptor_pool;
    std::unique_ptr<VulkanDescriptorSet> descriptor_set;

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
        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
            .setLayoutCount = 1,
            .pSetLayouts = descriptor_set_layout.Address()
        };
        return pipeline_layout.create(pipeline_layout_create_info) == VK_SUCCESS;
    }

    bool create_pipeline() {
        VulkanShaderModule vert(get_shader_path("VulkanTests/Texture.vert.spv").string().c_str());
        VulkanShaderModule frag(get_shader_path("VulkanTests/Texture.frag.spv").string().c_str());
        // static VkPipelineShaderStageCreateInfo shader_stage_create_infos_triangle[2] = {
        //     vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
        //     frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        // };
        static VkPipelineShaderStageCreateInfo shader_stage_create_infos_texture[2] = {
            vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        auto create = [&] {
            if (current_demo_name != "ImagelessFramebufferTest") return false;
            GraphicsPipelineCreateInfoPack pipeline_create_info_pack;
            pipeline_create_info_pack.create_info.layout = pipeline_layout;
            pipeline_create_info_pack.create_info.renderPass = get_shared_render_pass_imageless_framebuffer();
            // 子通道只有一个，pipeline_create_info_pack.createInfo.renderPass使用默认值0

            // vertex buffer
            //数据来自0号顶点缓冲区，输入频率是逐顶点输入
            pipeline_create_info_pack.vertex_input_bindings.emplace_back(0, sizeof(texture_vertex), VK_VERTEX_INPUT_RATE_VERTEX);
            // //location为0，数据来自0号顶点缓冲区，vec2对应VK_FORMAT_R32G32_SFLOAT，用offsetof计算position在vertex中的起始位置
            // pipeline_create_info_pack.vertex_input_attributes.emplace_back(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, position));
            // //location为1，数据来自0号顶点缓冲区，vec4对应VK_FORMAT_R32G32B32A32_SFLOAT，用offsetof计算color在vertex中的起始位置
            // pipeline_create_info_pack.vertex_input_attributes.emplace_back(1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(vertex, color));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(texture_vertex, position));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(texture_vertex, texCoord));

            // pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

            pipeline_create_info_pack.viewports.emplace_back(0.f, 0.f, float(window_size.width), float(window_size.height), 0.f, 1.f);
            pipeline_create_info_pack.scissors.emplace_back(VkOffset2D{},window_size);
            pipeline_create_info_pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            pipeline_create_info_pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
            pipeline_create_info_pack.update_all_arrays();
            pipeline_create_info_pack.create_info.stageCount = 2;
            pipeline_create_info_pack.create_info.pStages = shader_stage_create_infos_texture;

            // pipeline_triangle.create(pipeline_create_info_pack);
            if (pipeline.create(pipeline_create_info_pack) != VK_SUCCESS)
                return false;

            return true;
        };
        auto destroy = [this] {
            if (current_demo_name != "ImagelessFramebufferTest") return;
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
        VkDescriptorImageInfo image_info = {
            .sampler = *sampler,
            .imageView = texture_image->get_image_view(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };

        descriptor_set->write(image_info, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        return true;
    }
};
