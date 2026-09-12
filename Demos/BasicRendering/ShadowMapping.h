#pragma once
#include "../DemoBase3D.h"
#include "../../Geometry/Vertex.h"
#include "../../Geometry/Model.h"

#include "../../VulkanBase/components/VulkanTexture.h"
#include "../../VulkanBase/components/VulkanSampler.h"
#include "../../VulkanBase/components/VulkanMemory.h"
#include "../../VulkanBase/FrameGraph/FrameGraph.h"
#include "../../VulkanBase/FrameGraph/FrameGraphExecutor.h"


class ShadowMapping : public DemoBase3D {
public:
    ShadowMapping(GLFWwindow *window)
    : DemoBase3D("ShadowMapping", DemoCategoryType::BASIC_RENDERING, "",  window)
    {}
    ~ShadowMapping() override = default;

    bool initialize_scene_resources() override {
        allocate_command_buffer();
        load_assets();

        VkSamplerCreateInfo sampler_create_info = VulkanTexture2D::get_sampler_create_info();
        sampler = std::make_unique<VulkanSampler>(sampler_create_info);
        offscreen_depth_sampler = std::make_unique<VulkanDepthSampler>();


        initialize_camera();
        timer_speed *= 0.5f;
        register_glfw_callback();

        if (!create_compute_descriptor_resources() ||
            !create_descriptor_resources() ||
            !create_pipeline_layout() ||
            !create_pipeline()) {
            return false;
        }

        return true;
    }

    void cleanup_scene_resources() override {
        executor_.reset();
        // SharedResourceManager::get_singleton().get_shared_fence().wait_and_reset();
        // 清理资源
        descriptor_sets.~VulkanDescriptorSets();
        descriptor_pool.reset();
        compute_descriptor_sets.~SatComputeDescriptorSets();
        compute_descriptor_pool.reset();
        sampler.reset();
        offscreen_depth_sampler.reset();

        // 清理管线
        graphic_pipelines.~GraphicPipelines();
        compute_pipelines.~ComputePipelines();
        pipeline_layout.~VulkanPipelineLayout();
        pipeline_layout_offscreen.~VulkanPipelineLayout();
        compute_pipeline_layout.~VulkanPipelineLayout();
        descriptor_set_layout.~VulkanDescriptorSetLayout();
        descriptor_set_layout_offscreen.~VulkanDescriptorSetLayout();
        descriptor_set_layout_compute.~VulkanDescriptorSetLayout();

        // 清理回调
        clean_up_glfw_callback();
        free_command_buffer();
    }

    void render_frame() override {
        update_uniform_data();
        const auto current_image_index = VulkanSwapchainManager::get_singleton().get_current_image_index();
        const auto& swapchain_info = VulkanSwapchainManager::get_singleton().get_swapchain_create_info();
        const auto shadow_map_size = VulkanPipelineManager::get_singleton().get_shadow_map_size();
        const uint32_t W = shadow_map_size.width;
        const uint32_t H = shadow_map_size.height;
        const uint32_t blocksX = (W + sat_block_size - 1) / sat_block_size;
        const uint32_t blocksY = (H + sat_block_size - 1) / sat_block_size;

        // ---------------------------------------------------------------- 构图
        // 整帧一张图：Shadow(graphics) -> 6 个 SAT(compute) -> Scene(graphics)。
        // 所有 shadow map / SAT 图像由图拥有，layout 与同步全部由图规划（不再手写 barrier）。
        frame_graph_.reset();
        frame_graph_.set_name("ShadowMapping");

        framegraph::TextureDesc shadow_depth_desc;
        shadow_depth_desc.name = "ShadowDepth";
        shadow_depth_desc.format = VK_FORMAT_D16_UNORM;
        shadow_depth_desc.extent = VkExtent3D{ W, H, 1 };
        shadow_depth_desc.usage = framegraph::ImageUsage::DepthStencilAttachment | framegraph::ImageUsage::Sampled;
        shadow_depth_ = frame_graph_.create_texture(shadow_depth_desc);

        framegraph::TextureDesc shadow_vsm_desc;
        shadow_vsm_desc.name = "ShadowVsm";
        shadow_vsm_desc.format = VK_FORMAT_R32G32_SFLOAT;
        shadow_vsm_desc.extent = shadow_depth_desc.extent;
        shadow_vsm_desc.usage = framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::Storage | framegraph::ImageUsage::Sampled;
        shadow_vsm_ = frame_graph_.create_texture(shadow_vsm_desc);

        auto create_sat_texture = [this](const char* name, uint32_t width, uint32_t height) {
            framegraph::TextureDesc desc;
            desc.name = name;
            desc.format = VK_FORMAT_R32G32_SFLOAT;
            desc.extent = VkExtent3D{ width, height, 1 };
            desc.usage = framegraph::ImageUsage::Storage | framegraph::ImageUsage::Sampled;
            return frame_graph_.create_texture(desc);
        };
        sat_row_partial_ = create_sat_texture("SatRowPartial", W, H);
        sat_sat_row_ = create_sat_texture("SatRow", W, H);
        sat_col_partial_ = create_sat_texture("SatColPartial", W, H);
        sat_final_ = create_sat_texture("SatFinal", W, H);
        sat_row_block_sums_ = create_sat_texture("SatRowBlockSums", blocksX, H);
        sat_row_block_prefix_ = create_sat_texture("SatRowBlockPrefix", blocksX, H);
        sat_col_block_sums_ = create_sat_texture("SatColBlockSums", W, blocksY);
        sat_col_block_prefix_ = create_sat_texture("SatColBlockPrefix", W, blocksY);

        framegraph::TextureDesc scene_depth_desc;
        scene_depth_desc.name = "SceneDepth";
        scene_depth_desc.format = VulkanCore::get_singleton().get_vulkan_device().get_supported_depth_format();
        scene_depth_desc.extent = VkExtent3D{ swapchain_info.imageExtent.width, swapchain_info.imageExtent.height, 1 };
        scene_depth_desc.usage = framegraph::ImageUsage::DepthStencilAttachment | framegraph::ImageUsage::Sampled;
        scene_depth_ = frame_graph_.create_texture(scene_depth_desc);

        framegraph::TextureDesc color_desc;
        color_desc.name = "SwapchainImage";
        color_desc.format = swapchain_info.imageFormat;
        color_desc.extent = scene_depth_desc.extent;
        color_desc.usage = framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::Present;
        scene_color_ = frame_graph_.import_texture(
            color_desc, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
            /*externally_synchronized=*/false);

        frame_graph_.add_graphics_pass("Shadow")
            .write(shadow_depth_, framegraph::usage::depth_stencil_write())
            .write(shadow_vsm_, framegraph::usage::color_attachment_write())
            .execute([this](framegraph::PassContext& context) { record_shadow_pass(context); });

        frame_graph_.add_compute_pass("SatRowBlock")
            .read(shadow_vsm_, framegraph::usage::storage_read())
            .write(sat_row_partial_, framegraph::usage::storage_write())
            .write(sat_row_block_sums_, framegraph::usage::storage_write())
            .execute([this, blocksX, H](framegraph::PassContext& context) {
                record_sat_pass(compute_pipelines.sat_row_block, compute_descriptor_sets.row_block, blocksX, H, 1, context);
            });
        frame_graph_.add_compute_pass("SatRowScan")
            .read(sat_row_block_sums_, framegraph::usage::storage_read())
            .write(sat_row_block_prefix_, framegraph::usage::storage_write())
            .execute([this, H](framegraph::PassContext& context) {
                record_sat_pass(compute_pipelines.sat_row_block_scan, compute_descriptor_sets.row_block_scan, 1, H, 1, context);
            });
        frame_graph_.add_compute_pass("SatRowAdd")
            .read(sat_row_partial_, framegraph::usage::storage_read())
            .read(sat_row_block_prefix_, framegraph::usage::storage_read())
            .write(sat_sat_row_, framegraph::usage::storage_write())
            .execute([this, blocksX, H](framegraph::PassContext& context) {
                record_sat_pass(compute_pipelines.sat_row_block_add, compute_descriptor_sets.row_block_add, blocksX, H, 1, context);
            });
        frame_graph_.add_compute_pass("SatColBlock")
            .read(sat_sat_row_, framegraph::usage::storage_read())
            .write(sat_col_partial_, framegraph::usage::storage_write())
            .write(sat_col_block_sums_, framegraph::usage::storage_write())
            .execute([this, W, blocksY](framegraph::PassContext& context) {
                record_sat_pass(compute_pipelines.sat_col_block, compute_descriptor_sets.col_block, W, blocksY, 1, context);
            });
        frame_graph_.add_compute_pass("SatColScan")
            .read(sat_col_block_sums_, framegraph::usage::storage_read())
            .write(sat_col_block_prefix_, framegraph::usage::storage_write())
            .execute([this, W](framegraph::PassContext& context) {
                record_sat_pass(compute_pipelines.sat_col_block_scan, compute_descriptor_sets.col_block_scan, W, 1, 1, context);
            });
        frame_graph_.add_compute_pass("SatColAdd")
            .read(sat_col_partial_, framegraph::usage::storage_read())
            .read(sat_col_block_prefix_, framegraph::usage::storage_read())
            .write(sat_final_, framegraph::usage::storage_write())
            .execute([this, W, blocksY](framegraph::PassContext& context) {
                record_sat_pass(compute_pipelines.sat_col_block_add, compute_descriptor_sets.col_block_add, W, blocksY, 1, context);
            });

        frame_graph_.add_graphics_pass("Scene")
            .write(scene_color_, framegraph::usage::color_attachment_write())
            .write(scene_depth_, framegraph::usage::depth_stencil_write())
            .read(shadow_depth_, framegraph::usage::depth_stencil_sampled_read())
            .read(sat_final_, framegraph::usage::sampled_read())
            .execute([this](framegraph::PassContext& context) { record_scene_pass(context, scene_color_, scene_depth_); });
        // 呈现前的最终过渡：由图标出 COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
        //（dynamic rendering 没有隐式布局转换，ImGui overlay 的 render pass 要求 PRESENT_SRC_KHR）
        frame_graph_.add_transfer_pass("Present")
            .write(scene_color_, framegraph::usage::present())
            .execute([](framegraph::PassContext&) {});

        if (!frame_graph_.compile()) {
            outstream << std::format("[ ShadowMapping ] compile 失败: {}\n", frame_graph_.get_error());
            return;
        }
        executor_.import_texture(scene_color_,
                                 VulkanSwapchainManager::get_singleton().get_swapchain_image(current_image_index),
                                 VulkanSwapchainManager::get_singleton().get_swapchain_image_view(current_image_index));
        executor_.set_synchronization2(
            VulkanCore::get_singleton().get_vulkan_device().get_physical_device_vulkan13_features().synchronization2 == VK_TRUE);
        if (!executor_.prepare(frame_graph_)) {
            outstream << std::format("[ ShadowMapping ] prepare 失败: {}\n", executor_.get_error());
            return;
        }

        // 图拥有的资源每帧都会重新创建/复用：descriptor 必须在 prepare() 之后用图提供的 view 重写。
        VkDescriptorImageInfo shadow_depth_descriptor = {
            *offscreen_depth_sampler,
            executor_.image_view(shadow_depth_),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        };
        descriptor_sets.scene.write(shadow_depth_descriptor, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, 0);
        VkDescriptorImageInfo sat_final_descriptor = {
            *sampler,
            executor_.image_view(sat_final_),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        descriptor_sets.scene.write(sat_final_descriptor, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, 0);

        auto write_storage = [this](const VulkanDescriptorSet& set, framegraph::ResourceHandle resource, uint32_t binding) {
            VkDescriptorImageInfo info = { VK_NULL_HANDLE, executor_.image_view(resource), VK_IMAGE_LAYOUT_GENERAL };
            set.write(info, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, binding, 0);
        };
        write_storage(compute_descriptor_sets.row_block, shadow_vsm_, 0);
        write_storage(compute_descriptor_sets.row_block, sat_row_partial_, 1);
        write_storage(compute_descriptor_sets.row_block, sat_row_block_sums_, 2);
        write_storage(compute_descriptor_sets.row_block_scan, sat_row_block_sums_, 0);
        write_storage(compute_descriptor_sets.row_block_scan, sat_row_block_prefix_, 1);
        write_storage(compute_descriptor_sets.row_block_add, sat_row_partial_, 0);
        write_storage(compute_descriptor_sets.row_block_add, sat_row_block_prefix_, 1);
        write_storage(compute_descriptor_sets.row_block_add, sat_sat_row_, 2);
        write_storage(compute_descriptor_sets.col_block, sat_sat_row_, 0);
        write_storage(compute_descriptor_sets.col_block, sat_col_partial_, 1);
        write_storage(compute_descriptor_sets.col_block, sat_col_block_sums_, 2);
        write_storage(compute_descriptor_sets.col_block_scan, sat_col_block_sums_, 0);
        write_storage(compute_descriptor_sets.col_block_scan, sat_col_block_prefix_, 1);
        write_storage(compute_descriptor_sets.col_block_add, sat_col_partial_, 0);
        write_storage(compute_descriptor_sets.col_block_add, sat_col_block_prefix_, 1);
        write_storage(compute_descriptor_sets.col_block_add, sat_final_, 2);

        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        {
            executor_.execute(frame_graph_, command_buffer);

            VkClearValue clear_values[2] = {
                {.color = { 0.f, 0.f, 0.f, 1.f }},
                {.depthStencil = { 1.f, 0 }}
            };
            imgui_render(current_image_index, clear_values);
        }
        command_buffer.end();
    }



private:
    float zNear = 1.0f;
    float zFar = 96.0f;

    float depth_bias_constant = 1.25f;
    float depth_bias_slope = 1.75f;

    glm::vec3 light_pos = glm::vec3();
    float light_size = 2.5f;
    float light_fov = 45.f;
    VulkanglTFModel demo_scene;
    std::string loaded_scene_name;

    // 屏幕 pass 已经接入 FrameGraph（图拥有 depth，颜色用外部同步的 swapchain image）
    framegraph::FrameGraph frame_graph_;
    framegraph::FrameGraphExecutor executor_;

    // 每帧由图创建的资源句柄（图每帧重建，句柄随之更新）
    framegraph::ResourceHandle scene_color_{};
    framegraph::ResourceHandle scene_depth_{};
    framegraph::ResourceHandle shadow_depth_{};
    framegraph::ResourceHandle shadow_vsm_{};
    framegraph::ResourceHandle sat_row_partial_{};
    framegraph::ResourceHandle sat_sat_row_{};
    framegraph::ResourceHandle sat_col_partial_{};
    framegraph::ResourceHandle sat_final_{};
    framegraph::ResourceHandle sat_row_block_sums_{};
    framegraph::ResourceHandle sat_row_block_prefix_{};
    framegraph::ResourceHandle sat_col_block_sums_{};
    framegraph::ResourceHandle sat_col_block_prefix_{};

    int shadow_filter_mode = 0;

    struct UniformDataScene {
        glm::mat4 projection;
        glm::mat4 view;
        glm::mat4 model;
        glm::mat4 depth_bias_mvp;
        glm::vec4 light_pos;
        float light_size;
        // Used for depth map visualization
        float z_near;
        float z_far;
    } uniform_data_scene;

    struct UniformDataOffscreen {
        glm::mat4 depth_mvp;
        float z_near;
        float z_far;
    } uniform_data_offscreen;

    std::unique_ptr<VulkanSampler> sampler;
    std::unique_ptr<VulkanDepthSampler> offscreen_depth_sampler;
    std::unique_ptr<VulkanDescriptorPool> descriptor_pool;
    std::unique_ptr<VulkanDescriptorPool> compute_descriptor_pool;
    struct VulkanDescriptorSets{
        VulkanDescriptorSet offscreen;
        VulkanDescriptorSet scene;
        ~VulkanDescriptorSets() {
            offscreen.~VulkanDescriptorSet();
            scene.~VulkanDescriptorSet();
        }
    } descriptor_sets;
    struct SatComputeDescriptorSets {
        VulkanDescriptorSet row_block;
        VulkanDescriptorSet row_block_scan;
        VulkanDescriptorSet row_block_add;
        VulkanDescriptorSet col_block;
        VulkanDescriptorSet col_block_scan;
        VulkanDescriptorSet col_block_add;
        ~SatComputeDescriptorSets() {
            row_block.~VulkanDescriptorSet();
            row_block_scan.~VulkanDescriptorSet();
            row_block_add.~VulkanDescriptorSet();
            col_block.~VulkanDescriptorSet();
            col_block_scan.~VulkanDescriptorSet();
            col_block_add.~VulkanDescriptorSet();
        }
    } compute_descriptor_sets;

    struct UniformBuffers {
        std::unique_ptr<VulkanUniformBuffer> uniform_buffer_screen;
        std::unique_ptr<VulkanUniformBuffer> uniform_buffer_offscreen;
     } uniform_buffers;

    struct GraphicPipelines {
        VulkanPipeline offscreen;
        VulkanPipeline scene_shadow;
        VulkanPipeline scene_shadow_PCF;
        VulkanPipeline scene_shadow_PCSS;
        VulkanPipeline scene_shadow_VSSM;
        ~GraphicPipelines() {
            offscreen.~VulkanPipeline();
            scene_shadow.~VulkanPipeline();
            scene_shadow_PCF.~VulkanPipeline();
            scene_shadow_PCSS.~VulkanPipeline();
            scene_shadow_VSSM.~VulkanPipeline();
        }
    } graphic_pipelines;

    struct ComputePipelines {
        VulkanPipeline sat_row_block;
        VulkanPipeline sat_row_block_add;
        VulkanPipeline sat_row_block_scan;
        VulkanPipeline sat_col_block;
        VulkanPipeline sat_col_block_add;
        VulkanPipeline sat_col_block_scan;
        ~ComputePipelines() {
            sat_row_block.~VulkanPipeline();
            sat_row_block_scan.~VulkanPipeline();
            sat_row_block_add.~VulkanPipeline();
            sat_col_block.~VulkanPipeline();
            sat_col_block_scan.~VulkanPipeline();
            sat_col_block_add.~VulkanPipeline();
        }
    } compute_pipelines;

    // compute pipelines
    VulkanPipelineLayout compute_pipeline_layout;
    VulkanPipelineLayout pipeline_layout_offscreen;

    // SAT resources
    static constexpr uint32_t sat_block_size = 256;
    struct SatImages {
        // Full-resolution intermediate/final SAT images (W x H, RG32F)
        VulkanColorAttachment row_partial;
        VulkanColorAttachment sat_row;
        VulkanColorAttachment col_partial;
        VulkanColorAttachment sat_final;

        // Row block images (blocksX x H, RG32F)
        VulkanColorAttachment row_block_sums;
        VulkanColorAttachment row_block_prefix;

        // Column block images (W x blocksY, RG32F)
        VulkanColorAttachment col_block_sums;
        VulkanColorAttachment col_block_prefix;
    } sat_images;
    VulkanDescriptorSetLayout descriptor_set_layout_compute;
    VulkanDescriptorSetLayout descriptor_set_layout_offscreen;

    // compute pass 的录制：直接绑管线 / 描述符并 dispatch，同步与 layout 全部由图规划。
    void record_sat_pass(VkPipeline pipeline, const VulkanDescriptorSet& set, uint32_t x, uint32_t y, uint32_t z,
                         framegraph::PassContext& context) {
        auto* frame = static_cast<framegraph::FrameGraphExecution*>(context.user_data);
        const VkCommandBuffer cmd = frame->command_buffer;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_layout, 0, 1,
                                set.Address(), 0, nullptr);
        vkCmdDispatch(cmd, x, y, z);
    }

    // 阴影 pass 的录制：render pass / framebuffer 由图提供（与 offscreen 管线兼容，需 2 条 subpass 依赖）。
    void record_shadow_pass(framegraph::PassContext& context) {
        auto* frame = static_cast<framegraph::FrameGraphExecution*>(context.user_data);
        const VkCommandBuffer cmd = frame->command_buffer;
        const auto shadow_map_size = VulkanPipelineManager::get_singleton().get_shadow_map_size();

        const framegraph::RenderTargetAttachment attachments[2] = {
            { .resource = shadow_vsm_,
              .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .store_op = VK_ATTACHMENT_STORE_OP_STORE },
            { .resource = shadow_depth_,
              .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .store_op = VK_ATTACHMENT_STORE_OP_STORE,
              .stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .depth_stencil = true },
        };
        VkClearValue clear_values[2] = {
            {.color = { 1.0f, 1.0f, 0.0f, 0.0f }},
            {.depthStencil = { 1.f, 0 }}
        };
        const framegraph::FrameGraphExecutor::DynamicRenderingTarget* target =
            executor_.acquire_rendering_info(frame_graph_, attachments, clear_values);
        if (!target) {
            outstream << std::format("[ ShadowMapping ] 获取 shadow rendering info 失败: {}\n", executor_.get_error());
            return;
        }
        vkCmdBeginRendering(cmd, &target->info);
        {
            VkViewport viewport = {
                .width = static_cast<float>(shadow_map_size.width),
                .height = static_cast<float>(shadow_map_size.height),
                .minDepth = 0.f,
                .maxDepth = 1.f
            };
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor = { .offset = {0, 0}, .extent = {shadow_map_size.width, shadow_map_size.height} };
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdSetDepthBias(cmd, depth_bias_constant, 0.f, depth_bias_slope);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphic_pipelines.offscreen);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_offscreen, 0, 1,
                                    descriptor_sets.offscreen.Address(), 0, nullptr);
            draw(demo_scene, pipeline_layout_offscreen);
        }
        vkCmdEndRendering(cmd);
    }

    // 屏幕 pass 的录制：render pass / framebuffer 由图（executor）提供。
    void record_scene_pass(framegraph::PassContext& context, framegraph::ResourceHandle color,
                           framegraph::ResourceHandle depth) {
        auto* frame = static_cast<framegraph::FrameGraphExecution*>(context.user_data);
        const VkCommandBuffer cmd = frame->command_buffer;

        const framegraph::RenderTargetAttachment attachments[2] = {
            { .resource = color,
              .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
              .initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
              .final_layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
              .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .store_op = VK_ATTACHMENT_STORE_OP_STORE },
            { .resource = depth,
              .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
              .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
              .stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE,
              .depth_stencil = true },
        };
        VkClearValue clear_values[2] = {
            {.color = { 0.f, 0.f, 0.f, 1.f }},
            {.depthStencil = { 1.f, 0 }}
        };
        const framegraph::FrameGraphExecutor::DynamicRenderingTarget* target =
            executor_.acquire_rendering_info(frame_graph_, attachments, clear_values);
        if (!target) {
            outstream << std::format("[ ShadowMapping ] 获取 rendering info 失败: {}\n", executor_.get_error());
            return;
        }

        vkCmdBeginRendering(cmd, &target->info);
        {
            VkViewport viewport = {
                .width = static_cast<float>(window_size.width),
                .height = static_cast<float>(window_size.height),
                .minDepth = 0.f,
                .maxDepth = 1.f
            };
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            VkRect2D scissor = { .offset = {0, 0}, .extent = {window_size.width, window_size.height} };
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            switch (shadow_filter_mode) {
                case 0:
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphic_pipelines.scene_shadow);
                    break;
                case 1:
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphic_pipelines.scene_shadow_PCF);
                    break;
                case 2:
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphic_pipelines.scene_shadow_PCSS);
                    break;
                case 3:
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphic_pipelines.scene_shadow_VSSM);
                    break;
            }
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
                                    descriptor_sets.scene.Address(), 0, nullptr);
            draw(demo_scene, pipeline_layout);
        }
        vkCmdEndRendering(cmd);
    }
    void update_uniform_data() {
        update_light();

        // offscreen
        glm::mat4 depth_proj = glm::perspective(glm::radians(light_fov), 1.f, zNear, zFar);
        glm::mat4 depth_view = glm::lookAt(light_pos, glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
        glm::mat4 depth_model = glm::mat4(1.f);
        uniform_data_offscreen.depth_mvp = depth_proj * depth_view * depth_model;
        uniform_data_offscreen.z_near = zNear;
        uniform_data_offscreen.z_far = zFar;
        uniform_buffers.uniform_buffer_offscreen->transfer_data(uniform_data_offscreen);

        // screen
        uniform_data_scene.projection = camera.matrices.perspective;
        uniform_data_scene.view = camera.matrices.view;
        uniform_data_scene.model = glm::mat4(1.f);
        uniform_data_scene.light_pos = glm::vec4(light_pos, 1.f);
        uniform_data_scene.light_size = light_size;
        uniform_data_scene.depth_bias_mvp = uniform_data_offscreen.depth_mvp;
        uniform_data_scene.z_near = zNear;
        uniform_data_scene.z_far = zFar;
        uniform_buffers.uniform_buffer_screen->transfer_data(uniform_data_scene);
    }


    bool create_pipeline_layout() {
        VkPushConstantRange push_constant_range = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,  // 告诉Vulkan哪个阶段会访问它
            .offset = 0,                               // 偏移量
            .size = sizeof(glm::mat4)                  // 大小，对应你的 node_matrix
        };
        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
            .setLayoutCount = 1,
            .pSetLayouts = descriptor_set_layout.Address(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range
        };

        VkPipelineLayoutCreateInfo compute_pipeline_layout_create_info = {
            .setLayoutCount = 1,
            .pSetLayouts = descriptor_set_layout_compute.Address(),
            .pushConstantRangeCount = 0,
            .pPushConstantRanges = nullptr
        };

        VkPipelineLayoutCreateInfo pipeline_layout_offscreen_create_info = {
            .setLayoutCount = 1,
            .pSetLayouts = descriptor_set_layout_offscreen.Address(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range
        };

        return pipeline_layout.create(pipeline_layout_create_info) == VK_SUCCESS
            && pipeline_layout_offscreen.create(pipeline_layout_offscreen_create_info) == VK_SUCCESS
            && compute_pipeline_layout.create(compute_pipeline_layout_create_info) == VK_SUCCESS;

    }

    bool create_pipeline() {
        static VulkanShaderModule vert(get_shader_path("BasicRendering/ShadowMapping/scene.vert.spv").string().c_str());
        static VulkanShaderModule frag(get_shader_path("BasicRendering/ShadowMapping/scene.frag.spv").string().c_str());
        static VulkanShaderModule vert_offscreen(get_shader_path("BasicRendering/ShadowMapping/offscreen.vert.spv").string().c_str());
        static VulkanShaderModule frag_offscreen(get_shader_path("BasicRendering/ShadowMapping/offscreen.frag.spv").string().c_str());
        static VkPipelineShaderStageCreateInfo shader_stage_create_infos[2] = {
            vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        uint32_t filter_type = 0;
        VkSpecializationMapEntry specialization_map_entry = {
            0,
            0,
            sizeof(uint32_t)
        };
        VkSpecializationInfo specializationInfo = {
            1,
            &specialization_map_entry,
            sizeof(uint32_t),
            &filter_type
        };
        auto create = [&] {
            if (current_demo_name != "ShadowMapping") return false;
            GraphicsPipelineCreateInfoPack pipeline_create_info_pack;
            pipeline_create_info_pack.create_info.layout = pipeline_layout;
            // dynamic rendering：scene 管线用 VkPipelineRenderingCreateInfo 声明附件格式
            const VkFormat scene_color_format = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat;
            VkPipelineRenderingCreateInfo scene_rendering_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
            scene_rendering_info.colorAttachmentCount = 1;
            scene_rendering_info.pColorAttachmentFormats = &scene_color_format;
            scene_rendering_info.depthAttachmentFormat = VulkanCore::get_singleton().get_vulkan_device().get_supported_depth_format();
            pipeline_create_info_pack.create_info.renderPass = VK_NULL_HANDLE;
            pipeline_create_info_pack.create_info.pNext = &scene_rendering_info;
            // 子通道只有一个，pipeline_create_info_pack.createInfo.renderPass使用默认值0

            // vertex buffer
            pipeline_create_info_pack.vertex_input_bindings.emplace_back(0, sizeof(VulkanglTFModel::Vertex), VK_VERTEX_INPUT_RATE_VERTEX);
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, pos));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, normal));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanglTFModel::Vertex, uv));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanglTFModel::Vertex, color));

            // pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            pipeline_create_info_pack.viewports.emplace_back(0.f, 0.f, float(window_size.width), float(window_size.height), 0.f, 1.f);
            pipeline_create_info_pack.scissors.emplace_back(VkOffset2D{},window_size);
            pipeline_create_info_pack.rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
            pipeline_create_info_pack.rasterization_state_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            pipeline_create_info_pack.rasterization_state_create_info.polygonMode = VK_POLYGON_MODE_FILL;
            pipeline_create_info_pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            pipeline_create_info_pack.depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
            pipeline_create_info_pack.depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
            pipeline_create_info_pack.depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            pipeline_create_info_pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
            pipeline_create_info_pack.dynamic_states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
            pipeline_create_info_pack.dynamic_states.push_back(VK_DYNAMIC_STATE_SCISSOR);
            pipeline_create_info_pack.update_all_arrays();
            pipeline_create_info_pack.create_info.stageCount = 2;
            shader_stage_create_infos[1].pSpecializationInfo = &specializationInfo;
            pipeline_create_info_pack.create_info.pStages = shader_stage_create_infos;

            // no filtering
            if (graphic_pipelines.scene_shadow.create(pipeline_create_info_pack) != VK_SUCCESS)
                return false;

            // PCF
            filter_type = 1;
            if (graphic_pipelines.scene_shadow_PCF.create(pipeline_create_info_pack) != VK_SUCCESS)
                return false;

            // PCSS
            filter_type = 2;
            if (graphic_pipelines.scene_shadow_PCSS.create(pipeline_create_info_pack) != VK_SUCCESS)
                return false;

            // VSSM
            filter_type = 3;
            if (graphic_pipelines.scene_shadow_VSSM.create(pipeline_create_info_pack) != VK_SUCCESS)
                return false;

            // offscreen pipeline
            pipeline_create_info_pack.shader_stages.clear();
            pipeline_create_info_pack.shader_stages.push_back(
                vert_offscreen.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT)
            );
            pipeline_create_info_pack.shader_stages.push_back(
                frag_offscreen.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
            );
            pipeline_create_info_pack.create_info.layout = pipeline_layout_offscreen;
            // offscreen 阴影管线同样使用 dynamic rendering（VSM=R32G32_SFLOAT，深度=D16_UNORM）
            const VkFormat shadow_color_format = VK_FORMAT_R32G32_SFLOAT;
            VkPipelineRenderingCreateInfo shadow_rendering_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
            shadow_rendering_info.colorAttachmentCount = 1;
            shadow_rendering_info.pColorAttachmentFormats = &shadow_color_format;
            shadow_rendering_info.depthAttachmentFormat = VK_FORMAT_D16_UNORM;
            pipeline_create_info_pack.create_info.renderPass = VK_NULL_HANDLE;
            pipeline_create_info_pack.create_info.pNext = &shadow_rendering_info;
            pipeline_create_info_pack.color_blend_state_create_info.attachmentCount = 1;
            pipeline_create_info_pack.rasterization_state_create_info.cullMode = VK_CULL_MODE_NONE;
            pipeline_create_info_pack.rasterization_state_create_info.depthBiasEnable = VK_TRUE;
            pipeline_create_info_pack.dynamic_states.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
            pipeline_create_info_pack.update_all_arrays();
            pipeline_create_info_pack.create_info.stageCount = 2;

            if (graphic_pipelines.offscreen.create(pipeline_create_info_pack) != VK_SUCCESS)
                return false;

            return true;
        };
        auto destroy = [this] {
            if (current_demo_name != "ShadowMapping") return;
            graphic_pipelines.scene_shadow.~VulkanPipeline();
            graphic_pipelines.scene_shadow_PCF.~VulkanPipeline();
            graphic_pipelines.scene_shadow_PCSS.~VulkanPipeline();
            graphic_pipelines.scene_shadow_VSSM.~VulkanPipeline();
            graphic_pipelines.offscreen.~VulkanPipeline();
        };
        add_swapchain_create_callback(create);
        add_swapchain_destroy_callback(destroy);

        if (create() ==  false) return false;

        // create compute pipeline
        ComputePipelineCreateInfoPack compute_pipeline_create_info_pack;

        struct ComputeTask {
              const char* spv_path;
              VulkanPipeline ComputePipelines::* pipeline_member;
          };

        static constexpr ComputeTask kTasks[] = {
          {"BasicRendering/ShadowMapping/sat_row_block.comp.spv",      &ComputePipelines::sat_row_block},
          {"BasicRendering/ShadowMapping/sat_row_block_scan.comp.spv", &ComputePipelines::sat_row_block_scan},
          {"BasicRendering/ShadowMapping/sat_row_block_add.comp.spv",  &ComputePipelines::sat_row_block_add},
          {"BasicRendering/ShadowMapping/sat_col_block.comp.spv",      &ComputePipelines::sat_col_block},
          {"BasicRendering/ShadowMapping/sat_col_block_scan.comp.spv", &ComputePipelines::sat_col_block_scan},
          {"BasicRendering/ShadowMapping/sat_col_block_add.comp.spv",  &ComputePipelines::sat_col_block_add},
        };

        compute_pipeline_create_info_pack.create_info.layout = compute_pipeline_layout;
        compute_pipeline_create_info_pack.create_info.basePipelineHandle = VK_NULL_HANDLE;
        compute_pipeline_create_info_pack.create_info.basePipelineIndex = -1;

        for (const auto& t : kTasks) {
          VulkanShaderModule shader(get_shader_path(t.spv_path).string().c_str());
          compute_pipeline_create_info_pack.compute_stage_create_info = shader.stage_create_info(VK_SHADER_STAGE_COMPUTE_BIT);
          compute_pipeline_create_info_pack.update_all();

          if ((compute_pipelines.*(t.pipeline_member)).create(compute_pipeline_create_info_pack) != VK_SUCCESS)
              return false;
        }
        return true;
    }

    bool create_descriptor_resources() {
        VkDescriptorSetLayoutBinding descriptor_set_layout_bindings_scene[3] = {
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
            },
            {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
            },
            {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
            }
        };

        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info_scene = {
            .bindingCount = 3,
            .pBindings = descriptor_set_layout_bindings_scene
        };
        descriptor_set_layout.create(descriptor_set_layout_create_info_scene);

        VkDescriptorSetLayoutBinding descriptor_set_layout_bindings_offscreen[1] = {
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
            }
        };

        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info_offscreen = {
            .bindingCount = 1,
            .pBindings = descriptor_set_layout_bindings_offscreen
        };
        descriptor_set_layout_offscreen.create(descriptor_set_layout_create_info_offscreen);

        // 初始化uniform buffers
        uniform_buffers.uniform_buffer_screen = std::make_unique<VulkanUniformBuffer>(sizeof(uniform_data_scene));
        uniform_buffers.uniform_buffer_offscreen = std::make_unique<VulkanUniformBuffer>(sizeof(uniform_data_offscreen));

        uniform_buffers.uniform_buffer_screen->transfer_data(uniform_data_scene);
        uniform_buffers.uniform_buffer_offscreen->transfer_data(uniform_data_offscreen);

        // 创建描述符池
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  2 }
        };

        descriptor_pool = std::make_unique<VulkanDescriptorPool>(2, pool_sizes);

        VkDescriptorImageInfo shadow_map_descriptor = {*offscreen_depth_sampler, VulkanPipelineManager::get_singleton().get_dsa_offscreen().get_image_view(),VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
        VkDescriptorBufferInfo buffer_infos[] = {
            { *uniform_buffers.uniform_buffer_screen, 0, VK_WHOLE_SIZE },
            { *uniform_buffers.uniform_buffer_offscreen, 0, VK_WHOLE_SIZE}
        };
        // 描述符
        descriptor_pool->allocate_sets(descriptor_sets.offscreen, descriptor_set_layout_offscreen);
        descriptor_sets.offscreen.write(buffer_infos[1],VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 0);

        descriptor_pool->allocate_sets(descriptor_sets.scene, descriptor_set_layout);
        descriptor_sets.scene.write(buffer_infos[0],VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 0);
        descriptor_sets.scene.write(shadow_map_descriptor,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, 0);

        VkDescriptorImageInfo sat_scene_descriptor = {
            *sampler, // 或独立 sat_sampler
            sat_images.sat_final.get_image_view(),
            VK_IMAGE_LAYOUT_GENERAL
        };
        descriptor_sets.scene.write(sat_scene_descriptor,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, 0);
        return true;
    }

    bool create_compute_descriptor_resources() {
        const auto shadow_map_size = VulkanPipelineManager::get_singleton().get_shadow_map_size();
        const uint32_t blocks_x = (shadow_map_size.width + sat_block_size - 1) / sat_block_size;
        const uint32_t blocks_y = (shadow_map_size.height + sat_block_size - 1) / sat_block_size;

        constexpr VkImageUsageFlags sat_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
        sat_images.row_partial.create(VK_FORMAT_R32G32_SFLOAT, shadow_map_size, 1, VK_SAMPLE_COUNT_1_BIT, sat_usage);
        sat_images.sat_row.create(VK_FORMAT_R32G32_SFLOAT, shadow_map_size, 1, VK_SAMPLE_COUNT_1_BIT, sat_usage);
        sat_images.col_partial.create(VK_FORMAT_R32G32_SFLOAT, shadow_map_size, 1, VK_SAMPLE_COUNT_1_BIT, sat_usage);
        sat_images.sat_final.create(VK_FORMAT_R32G32_SFLOAT, shadow_map_size, 1, VK_SAMPLE_COUNT_1_BIT, sat_usage);

        sat_images.row_block_sums.create(VK_FORMAT_R32G32_SFLOAT, {blocks_x, shadow_map_size.height}, 1, VK_SAMPLE_COUNT_1_BIT, sat_usage);
        sat_images.row_block_prefix.create(VK_FORMAT_R32G32_SFLOAT, {blocks_x, shadow_map_size.height}, 1, VK_SAMPLE_COUNT_1_BIT, sat_usage);
        sat_images.col_block_sums.create(VK_FORMAT_R32G32_SFLOAT, {shadow_map_size.width, blocks_y}, 1, VK_SAMPLE_COUNT_1_BIT, sat_usage);
        sat_images.col_block_prefix.create(VK_FORMAT_R32G32_SFLOAT, {shadow_map_size.width, blocks_y}, 1, VK_SAMPLE_COUNT_1_BIT, sat_usage);

        VkDescriptorSetLayoutBinding compute_bindings[3] = {
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            },
            {
                .binding = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            },
            {
                .binding = 2,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
            }
        };
        VkDescriptorSetLayoutCreateInfo compute_layout_create_info = {
            .bindingCount = 3,
            .pBindings = compute_bindings
        };
        if (descriptor_set_layout_compute.create(compute_layout_create_info) != VK_SUCCESS)
            return false;

        VkDescriptorPoolSize compute_pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 18 }
        };
        compute_descriptor_pool = std::make_unique<VulkanDescriptorPool>(6, compute_pool_sizes);

        compute_descriptor_pool->allocate_sets(compute_descriptor_sets.row_block, descriptor_set_layout_compute);
        compute_descriptor_pool->allocate_sets(compute_descriptor_sets.row_block_scan, descriptor_set_layout_compute);
        compute_descriptor_pool->allocate_sets(compute_descriptor_sets.row_block_add, descriptor_set_layout_compute);
        compute_descriptor_pool->allocate_sets(compute_descriptor_sets.col_block, descriptor_set_layout_compute);
        compute_descriptor_pool->allocate_sets(compute_descriptor_sets.col_block_scan, descriptor_set_layout_compute);
        compute_descriptor_pool->allocate_sets(compute_descriptor_sets.col_block_add, descriptor_set_layout_compute);

        auto storage_image_info = [](VkImageView image_view) {
            return VkDescriptorImageInfo{
                .sampler = VK_NULL_HANDLE,
                .imageView = image_view,
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL
            };
        };

        const auto moments_raw = storage_image_info(VulkanPipelineManager::get_singleton().get_ca_offscreen_vsm().get_image_view());
        const auto row_partial = storage_image_info(sat_images.row_partial.get_image_view());
        const auto sat_row = storage_image_info(sat_images.sat_row.get_image_view());
        const auto col_partial = storage_image_info(sat_images.col_partial.get_image_view());
        const auto sat_final = storage_image_info(sat_images.sat_final.get_image_view());
        const auto row_block_sums = storage_image_info(sat_images.row_block_sums.get_image_view());
        const auto row_block_prefix = storage_image_info(sat_images.row_block_prefix.get_image_view());
        const auto col_block_sums = storage_image_info(sat_images.col_block_sums.get_image_view());
        const auto col_block_prefix = storage_image_info(sat_images.col_block_prefix.get_image_view());

        compute_descriptor_sets.row_block.write(moments_raw, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, 0);
        compute_descriptor_sets.row_block.write(row_partial, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0);
        compute_descriptor_sets.row_block.write(row_block_sums, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2, 0);

        compute_descriptor_sets.row_block_scan.write(row_block_sums, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, 0);
        compute_descriptor_sets.row_block_scan.write(row_block_prefix, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0);

        compute_descriptor_sets.row_block_add.write(row_partial, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, 0);
        compute_descriptor_sets.row_block_add.write(row_block_prefix, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0);
        compute_descriptor_sets.row_block_add.write(sat_row, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2, 0);

        compute_descriptor_sets.col_block.write(sat_row, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, 0);
        compute_descriptor_sets.col_block.write(col_partial, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0);
        compute_descriptor_sets.col_block.write(col_block_sums, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2, 0);

        compute_descriptor_sets.col_block_scan.write(col_block_sums, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, 0);
        compute_descriptor_sets.col_block_scan.write(col_block_prefix, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0);

        compute_descriptor_sets.col_block_add.write(col_partial, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, 0);
        compute_descriptor_sets.col_block_add.write(col_block_prefix, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, 0);
        compute_descriptor_sets.col_block_add.write(sat_final, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2, 0);

        return true;
    }

    void draw_node(VulkanglTFModel &model, VulkanglTFModel::Node* node, VkPipelineLayout active_pipeline_layout) {
        if (!node->mesh.primitives.empty()) {
            glm::mat4 node_matrix = node->matrix;
            VulkanglTFModel::Node* current_parent = node->parent;
            while (current_parent) {
                node_matrix = current_parent->matrix * node_matrix;
                current_parent = current_parent->parent;
            }
            glm::mat4 flip_matrix = glm::mat4(1.0f);
            flip_matrix[1][1] = -1.0f;
            glm::mat4 final_matrix = flip_matrix * node_matrix;

            vkCmdPushConstants(command_buffer, active_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &final_matrix);
            for (VulkanglTFModel::Primitive& primitive : node->mesh.primitives) {
                if (primitive.index_count > 0) {
                    vkCmdDrawIndexed(command_buffer, primitive.index_count, 1, primitive.first_index, 0, 0);
                }
            }
        }   
        for (auto& child : node->children) {
            draw_node(model, child, active_pipeline_layout);
        }
    }

    void draw(VulkanglTFModel &model, VkPipelineLayout active_pipeline_layout) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command_buffer, 0, 1, model.vertices.Address(), &offset);
        vkCmdBindIndexBuffer(command_buffer, model.indices.index_buffer, 0, VK_INDEX_TYPE_UINT32);
        for (auto& node : model.nodes) {
            draw_node(model, node, active_pipeline_layout);
        }
    }

    void load_glTF_file(const std::string& filename) {
        tinygltf::Model gltf_input;
        tinygltf::TinyGLTF gltf_context;
        std::string error, warning;

        bool file_loaded = gltf_context.LoadASCIIFromFile(&gltf_input, &error, &warning, filename);

        std::vector<uint32_t> index_buffer;
        std::vector<VulkanglTFModel::Vertex> vertex_buffer;

        if (file_loaded) {
            const tinygltf::Scene& scene = gltf_input.scenes[0];
            for (int n : scene.nodes) {
                const tinygltf::Node node = gltf_input.nodes[n];
                demo_scene.load_node(node, gltf_input, nullptr, index_buffer, vertex_buffer);
            }
        }
        else {
            outstream << std::format("[ Model ] Could not open the glTF file.\nMake sure the assets submodule has been checked out and is up-to-date.\n");
            return;
        }

        size_t vertex_buffer_size = vertex_buffer.size() * sizeof(VulkanglTFModel::Vertex);
        size_t index_buffer_size = index_buffer.size() * sizeof(uint32_t);
        demo_scene.indices.count = static_cast<uint32_t>(index_buffer.size());

        if (vertex_buffer_size > 0) {
            demo_scene.vertices.create(vertex_buffer_size);
            demo_scene.vertices.transfer_data(vertex_buffer.data(), vertex_buffer_size);
        }
        if (index_buffer_size > 0) {
            demo_scene.indices.index_buffer.create(index_buffer_size);
            demo_scene.indices.index_buffer.transfer_data(index_buffer.data(), index_buffer_size);
        }
    }

    void load_assets() {
        // --scene 指定时优先；否则用默认 TeapotsAndPillars。
        auto model_path = G_PROJECT_ROOT / "Assets/models/TeapotsAndPillars.gltf";
        if (!command_line_scene.empty())
            model_path = resolve_scene_asset(command_line_scene);
        loaded_scene_name = model_path.filename().string();
        load_glTF_file(model_path.string());
    }

    void update_light() {
        light_pos.x = cos(glm::radians(timer * 360.0f)) * 40.0f;
        light_pos.y = -50.0f + sin(glm::radians(timer * 360.0f)) * 20.0f;
        light_pos.z = 25.0f + sin(glm::radians(timer * 360.0f)) * 5.0f;
        // light_pos = glm::vec3(0.f, 0.f, 0.f);
        // light_pos = glm::vec3(0.f, 0.f, 0.f);
        // light_pos = glm::vec3(0.f, 0.f, -10.f);
    }

    void initialize_camera() {
        camera.flip_y = false;
        camera.set_perspective(60.0f, (float)window_size.width / (float)window_size.height, 1.f, 256.0f);
        camera.set_rotation({ -25.0f, -390.0f, 0.0f });
        camera.set_position({ 0.0f, 0.0f, -12.5f});
    }

    void draw_custom_ui() override {
        ImGui::Begin("Shadow filter Debug");
        const char* filter_items[] = { "None", "PCF", "PCSS", "VSSM"};
        ImGui::Combo("Filter Type", &shadow_filter_mode, filter_items, IM_ARRAYSIZE(filter_items));
        if (shadow_filter_mode == 2 || shadow_filter_mode == 3) {
            ImGui::SliderFloat("Light Size", &light_size, 0.1f, 5.0f, "%.2f");
        }
        ImGui::End();
    }
};
