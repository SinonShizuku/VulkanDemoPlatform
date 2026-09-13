#pragma once

#include "../DemoBase3D.h"
#include "../../VulkanBase/components/VulkanTexture.h"
#include "../../VulkanBase/components/VulkanTextureCube.h"
#include "../../VulkanBase/components/VulkanSampler.h"
#include "../../VulkanBase/components/VulkanDescriptor.h"
#include "../../VulkanBase/components/VulkanMemory.h"
#include "../../VulkanBase/FrameGraph/FrameGraph.h"
#include "../../VulkanBase/FrameGraph/FrameGraphExecutor.h"

// §16 主线 demo：PBR + IBL（最终目标场景是 Bistro 的 FBX spec/gloss 资产）。
//
// M1.1（当前）：cubemap 基础设施验证——程序化生成一张 6 面 test cubemap
// （每面一个主色 + U/V 渐变，便于截图核对朝向），再用 skybox pass 按视线方向采样。
// M1.2~M1.6（后续）：把 test cube 换成 Bistro 的 HDR 环境（equirect → cube），
// 生成 irradiance / prefiltered / BRDF LUT，再接入材质与 PBR 主 pass。
class PbrIbl : public DemoBase3D {
public:
    PbrIbl(GLFWwindow* window)
        : DemoBase3D("PbrIbl", DemoCategoryType::BASIC_RENDERING, "", window) {}
    ~PbrIbl() override = default;

    bool initialize_scene_resources() override {
        allocate_command_buffer();
        create_environment_cube();

        VkSamplerCreateInfo sampler_create_info = VulkanTexture2D::get_sampler_create_info();
        sampler = std::make_unique<VulkanSampler>(sampler_create_info);

        if (!create_descriptor_resources() || !create_pipeline_layout() || !create_pipeline())
            return false;

        initialize_camera();
        register_glfw_callback();
        executor_.set_synchronization2(
            VulkanCore::get_singleton().get_vulkan_device().get_physical_device_vulkan13_features().synchronization2 == VK_TRUE);
        return true;
    }

    void cleanup_scene_resources() override {
        executor_.reset();
        descriptor_set.reset();
        descriptor_pool.reset();
        sampler.reset();
        pipeline.~VulkanPipeline();
        pipeline_layout.~VulkanPipelineLayout();
        descriptor_set_layout.~VulkanDescriptorSetLayout();
        environment_cube.~VulkanTextureCube();
        clean_up_glfw_callback();
        free_command_buffer();
    }

    void render_frame() override {
        const uint32_t current_image_index = VulkanSwapchainManager::get_singleton().get_current_image_index();
        const auto& swapchain_info = VulkanSwapchainManager::get_singleton().get_swapchain_create_info();

        frame_graph_.reset();
        frame_graph_.set_name("PbrIbl");

        framegraph::TextureDesc color_desc;
        color_desc.name = "SwapchainImage";
        color_desc.format = swapchain_info.imageFormat;
        color_desc.extent = VkExtent3D{ swapchain_info.imageExtent.width, swapchain_info.imageExtent.height, 1 };
        color_desc.usage = framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::Present;
        const framegraph::ResourceHandle color = frame_graph_.import_texture(
            color_desc, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
            /*externally_synchronized=*/false);

        frame_graph_.add_graphics_pass("Skybox")
            .write(color, framegraph::usage::color_attachment_write())
            .execute([this, color](framegraph::PassContext& context) { record_skybox_pass(context, color); });
        frame_graph_.add_transfer_pass("Present")
            .write(color, framegraph::usage::present())
            .execute([](framegraph::PassContext&) {});

        if (!frame_graph_.compile()) {
            outstream << std::format("[ PbrIbl ] compile 失败: {}\n", frame_graph_.get_error());
            return;
        }
        executor_.import_texture(color,
                                 VulkanSwapchainManager::get_singleton().get_swapchain_image(current_image_index),
                                 VulkanSwapchainManager::get_singleton().get_swapchain_image_view(current_image_index));
        if (!executor_.prepare(frame_graph_)) {
            outstream << std::format("[ PbrIbl ] prepare 失败: {}\n", executor_.get_error());
            return;
        }

        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        {
            executor_.execute(frame_graph_, command_buffer);
            VkClearValue clear_values[2] = {
                {.color = { 0.02f, 0.02f, 0.03f, 1.f }},
                {.depthStencil = { 1.f, 0 }}
            };
            imgui_render(current_image_index, clear_values);
        }
        command_buffer.end();
    }

    void draw_custom_ui() override {
        if (ImGui::Begin("PBR + IBL (§16)")) {
            ImGui::Text("stage: M1.1 cubemap + skybox");
            ImGui::Text("environment cube: %ux%u x 6 faces (%s)", environment_cube.get_size(), environment_cube.get_size(),
                        "RGBA8 test pattern");
            ImGui::Text("mip levels: %u", environment_cube.get_mip_level_count());
        }
        ImGui::End();
    }

private:
    framegraph::FrameGraph frame_graph_;
    framegraph::FrameGraphExecutor executor_;
    VulkanTextureCube environment_cube;
    std::unique_ptr<VulkanSampler> sampler;
    std::unique_ptr<VulkanDescriptorPool> descriptor_pool;
    std::unique_ptr<VulkanDescriptorSet> descriptor_set;
    VulkanDescriptorSetLayout descriptor_set_layout;
    VulkanPipelineLayout pipeline_layout;
    VulkanPipeline pipeline;

    struct PushConstants {
        glm::mat4 inv_view_projection;
        glm::vec4 camera_position;
    };

    // 6 个面各一个主色 + U/V 渐变：用截图核对"哪一面、朝向对不对"（M1.1 的验收手段）。
    void create_environment_cube() {
        constexpr uint32_t k_face_size = 256;
        environment_cube.create(k_face_size, VK_FORMAT_R8G8B8A8_UNORM, 1);

        const glm::vec3 face_colors[6] = {
            { 1.0f, 0.25f, 0.25f },     // +X 红
            { 0.25f, 1.0f, 0.25f },     // -X 绿
            { 0.25f, 0.35f, 1.0f },     // +Y 蓝
            { 1.0f, 1.0f, 0.25f },      // -Y 黄
            { 1.0f, 0.25f, 1.0f },      // +Z 品红
            { 0.25f, 1.0f, 1.0f },      // -Z 青
        };

        std::vector<uint8_t> pixels(static_cast<size_t>(k_face_size) * k_face_size * 4);
        for (uint32_t face = 0; face < 6; ++face) {
            for (uint32_t y = 0; y < k_face_size; ++y) {
                for (uint32_t x = 0; x < k_face_size; ++x) {
                    const float u = static_cast<float>(x) / static_cast<float>(k_face_size - 1);
                    const float v = static_cast<float>(y) / static_cast<float>(k_face_size - 1);
                    const glm::vec3 color = face_colors[face] * (0.25f + 0.75f * u) * (0.55f + 0.45f * v);
                    uint8_t* texel = pixels.data() + (static_cast<size_t>(y) * k_face_size + x) * 4;
                    texel[0] = static_cast<uint8_t>(glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
                    texel[1] = static_cast<uint8_t>(glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
                    texel[2] = static_cast<uint8_t>(glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
                    texel[3] = 255;
                }
            }
            environment_cube.upload_face(face, 0, pixels.data(), pixels.size());
        }
        outstream << std::format("[ PbrIbl ] test environment cube: {}x{} x 6 faces（M1.1 占位环境，后续替换为 Bistro HDR → cube）\n",
                                 k_face_size, k_face_size);
    }

    bool create_descriptor_resources() {
        VkDescriptorSetLayoutBinding binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        };
        VkDescriptorSetLayoutCreateInfo layout_create_info = {
            .bindingCount = 1,
            .pBindings = &binding
        };
        if (descriptor_set_layout.create(layout_create_info) != VK_SUCCESS)
            return false;

        VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } };
        descriptor_pool = std::make_unique<VulkanDescriptorPool>(1, pool_sizes);
        descriptor_set = std::make_unique<VulkanDescriptorSet>();
        if (descriptor_pool->allocate_sets(*descriptor_set, descriptor_set_layout) != VK_SUCCESS)
            return false;

        VkDescriptorImageInfo image_info = environment_cube.get_descriptor_image_info(*sampler);
        descriptor_set->write(image_info, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, 0);
        return true;
    }

    bool create_pipeline_layout() {
        VkPushConstantRange push_constant_range = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(PushConstants)
        };
        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
            .setLayoutCount = 1,
            .pSetLayouts = descriptor_set_layout.Address(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range
        };
        return pipeline_layout.create(pipeline_layout_create_info) == VK_SUCCESS;
    }

    bool create_pipeline() {
        static VulkanShaderModule vert(get_shader_path("BasicRendering/pbrIbl/skybox.vert.spv").string().c_str());
        static VulkanShaderModule frag(get_shader_path("BasicRendering/pbrIbl/skybox.frag.spv").string().c_str());
        static VkPipelineShaderStageCreateInfo shader_stages[2] = {
            vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };

        auto create = [&] {
            if (current_demo_name != get_type())
                return false;
            const VkFormat color_format = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat;
            VkPipelineRenderingCreateInfo rendering_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
            rendering_info.colorAttachmentCount = 1;
            rendering_info.pColorAttachmentFormats = &color_format;

            GraphicsPipelineCreateInfoPack pack;
            pack.create_info.layout = pipeline_layout;
            pack.create_info.renderPass = VK_NULL_HANDLE;
            pack.create_info.pNext = &rendering_info;
            pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            pack.viewports.emplace_back(0.f, 0.f, float(window_size.width), float(window_size.height), 0.f, 1.f);
            pack.scissors.emplace_back(VkOffset2D{}, window_size);
            pack.rasterization_state_create_info.cullMode = VK_CULL_MODE_NONE;   // 全屏三角形
            pack.rasterization_state_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            pack.depth_stencil_state_create_info.depthTestEnable = VK_FALSE;
            pack.depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
            pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
            pack.update_all_arrays();
            pack.create_info.stageCount = 2;
            pack.create_info.pStages = shader_stages;
            return pipeline.create(pack) == VK_SUCCESS;
        };
        auto destroy = [this] {
            if (current_demo_name != get_type())
                return;
            pipeline.~VulkanPipeline();
        };
        add_swapchain_create_callback(create);
        add_swapchain_destroy_callback(destroy);
        return create();
    }

    void record_skybox_pass(framegraph::PassContext& context, framegraph::ResourceHandle color) {
        auto* frame = static_cast<framegraph::FrameGraphExecution*>(context.user_data);
        const VkCommandBuffer cmd = frame->command_buffer;

        const framegraph::RenderTargetAttachment attachments[1] = {
            { .resource = color,
              .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
              .final_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
              .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .store_op = VK_ATTACHMENT_STORE_OP_STORE },
        };
        VkClearValue clear_values[1] = {
            {.color = { 0.02f, 0.02f, 0.03f, 1.f }},
        };
        const framegraph::FrameGraphExecutor::DynamicRenderingTarget* target =
            executor_.acquire_rendering_info(frame_graph_, attachments, clear_values);
        if (!target) {
            outstream << std::format("[ PbrIbl ] 获取 rendering info 失败: {}\n", executor_.get_error());
            return;
        }

        const glm::mat4 view_projection = camera.matrices.perspective * camera.matrices.view;
        const PushConstants push_constants{
            .inv_view_projection = glm::inverse(view_projection),
            .camera_position = glm::vec4(glm::vec3(glm::inverse(camera.matrices.view) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)), 1.0f)
        };

        vkCmdBeginRendering(cmd, &target->info);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, descriptor_set->Address(), 0, nullptr);
        vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &push_constants);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
    }

    void initialize_camera() {
        camera.flip_y = true;
        camera.set_perspective(60.0f, (float)window_size.width / (float)window_size.height, 0.1f, 256.0f);
        // 稍微偏航一点，让画面同时看到两个面 + 一条接缝，便于核对朝向
        camera.set_rotation({ 0.0f, 30.0f, 0.0f });
        camera.set_position({ 0.0f, 0.0f, -1.0f });
    }
};