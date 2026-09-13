#pragma once

#include <chrono>

#include "../DemoBase3D.h"
#include "../../Interaction/HdrImage.h"
#include "../../VulkanBase/components/VulkanTexture.h"
#include "../../VulkanBase/components/VulkanTextureCube.h"
#include "../../VulkanBase/components/VulkanSampler.h"
#include "../../VulkanBase/components/VulkanDescriptor.h"
#include "../../VulkanBase/components/VulkanMemory.h"
#include "../../VulkanBase/FrameGraph/FrameGraph.h"
#include "../../VulkanBase/FrameGraph/FrameGraphExecutor.h"

// §16 主线 demo：PBR + IBL（目标场景 Bistro 的 FBX spec/gloss 资产）。
//
// M1.2（当前）：从 Bistro 自带的 equirect HDR 生成一整套 split-sum IBL 资源：
//     equirect → env cube(512², 10 mip) → irradiance(32²) / prefiltered(128², 6 mip) / BRDF LUT(512² RG16F)
//   bake 走一次性 command buffer（不进 FrameGraph，理由见 §16.5），完成后用 skybox pass 目视验证环境。
// M1.3~M1.6（后续）：材质扩展（spec/gloss 填参）、切线、PBR 主 pass、验收。
class PbrIbl : public DemoBase3D {
public:
    PbrIbl(GLFWwindow* window)
        : DemoBase3D("PbrIbl", DemoCategoryType::BASIC_RENDERING, "", window) {}
    ~PbrIbl() override = default;

    bool initialize_scene_resources() override {
        allocate_command_buffer();

        VkSamplerCreateInfo sampler_create_info = VulkanTexture2D::get_sampler_create_info();
        sampler = std::make_unique<VulkanSampler>(sampler_create_info);

        if (!create_bake_resources())
            return false;
        bake_ibl();
        if (!create_skybox_resources())
            return false;
        create_environment_cube_descriptor();

        initialize_camera();
        register_glfw_callback();
        executor_.set_synchronization2(
            VulkanCore::get_singleton().get_vulkan_device().get_physical_device_vulkan13_features().synchronization2 == VK_TRUE);
        return true;
    }

    void cleanup_scene_resources() override {
        executor_.reset();
        skybox_descriptor_set.reset();
        descriptor_set_layout.~VulkanDescriptorSetLayout();
        descriptor_pool.reset();
        skybox_pipeline.~VulkanPipeline();
        skybox_pipeline_layout.~VulkanPipelineLayout();

        equirect_pipeline.~VulkanPipeline();
        irradiance_pipeline.~VulkanPipeline();
        prefilter_pipeline.~VulkanPipeline();
        lut_pipeline.~VulkanPipeline();
        cube_bake_layout.~VulkanPipelineLayout();
        lut_layout.~VulkanPipelineLayout();
        equirect_set.~VulkanDescriptorSet();
        env_set.~VulkanDescriptorSet();
        bake_set_layout.~VulkanDescriptorSetLayout();
        bake_descriptor_pool.reset();

        environment_cube.~VulkanTextureCube();
        irradiance_cube.~VulkanTextureCube();
        prefiltered_cube.~VulkanTextureCube();
        lut_view.~VulkanImageView();
        lut_image.~VulkanImageMemory();
        equirect_hdr.~VulkanTexture2D();
        sampler.reset();

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
            ImGui::Text("stage: M1.2 IBL bake");
            ImGui::Text("environment: %s", environment_source.c_str());
            ImGui::Text("env cube    : %u²  %u mip  RGBA16F", environment_cube.get_size(), environment_cube.get_mip_level_count());
            ImGui::Text("irradiance  : %u²  %u mip  RGBA16F", irradiance_cube.get_size(), irradiance_cube.get_mip_level_count());
            ImGui::Text("prefiltered : %u²  %u mip  RGBA16F", prefiltered_cube.get_size(), prefiltered_cube.get_mip_level_count());
            ImGui::Text("BRDF LUT    : %u²  RG16F", k_lut_size);
            ImGui::Text("bake total  : %.1f ms", bake_ms);
        }
        ImGui::End();
    }

private:
    // §16.5 的尺寸约定（参考实现是 64²/512²(10 mip)/512²，我们先按更省的取值跑通）
    static constexpr uint32_t k_env_size = 512;
    static constexpr uint32_t k_env_mips = 10;
    static constexpr uint32_t k_irradiance_size = 32;
    static constexpr uint32_t k_prefilter_size = 128;
    static constexpr uint32_t k_prefilter_mips = 6;
    static constexpr uint32_t k_lut_size = 512;
    static constexpr uint32_t k_prefilter_samples = 32;
    static constexpr VkFormat k_hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat k_lut_format = VK_FORMAT_R16G16_SFLOAT;

    struct CubePushConstants {
        uint32_t face = 0;
        float param0 = 0.0f;    // irradiance: deltaPhi / prefilter: roughness
        float param1 = 0.0f;    // irradiance: deltaTheta / prefilter: 采样数
        float param2 = 0.0f;
    };

    framegraph::FrameGraph frame_graph_;
    framegraph::FrameGraphExecutor executor_;

    std::unique_ptr<VulkanSampler> sampler;
    VulkanTexture2D equirect_hdr;
    VulkanTextureCube environment_cube;
    VulkanTextureCube irradiance_cube;
    VulkanTextureCube prefiltered_cube;
    VulkanImageMemory lut_image;
    VulkanImageView lut_view;

    std::unique_ptr<VulkanDescriptorPool> bake_descriptor_pool;
    VulkanDescriptorSetLayout bake_set_layout;
    VulkanDescriptorSet equirect_set;
    VulkanDescriptorSet env_set;
    VulkanPipelineLayout cube_bake_layout;
    VulkanPipelineLayout lut_layout;
    VulkanPipeline equirect_pipeline;
    VulkanPipeline irradiance_pipeline;
    VulkanPipeline prefilter_pipeline;
    VulkanPipeline lut_pipeline;

    std::unique_ptr<VulkanDescriptorPool> descriptor_pool;
    std::unique_ptr<VulkanDescriptorSet> skybox_descriptor_set;
    VulkanDescriptorSetLayout descriptor_set_layout;
    VulkanPipelineLayout skybox_pipeline_layout;
    VulkanPipeline skybox_pipeline;

    std::string environment_source = "(none)";
    double bake_ms = 0.0;

    // ---------------------------------------------------------------- bake

    void bake_ibl() {
        const auto start = std::chrono::high_resolution_clock::now();

        environment_cube.create(k_env_size, k_hdr_format, k_env_mips, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        irradiance_cube.create(k_irradiance_size, k_hdr_format, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        prefiltered_cube.create(k_prefilter_size, k_hdr_format, k_prefilter_mips, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        create_brdf_lut_image();

        VkDescriptorImageInfo equirect_info = equirect_hdr.get_descriptor_image_info(*sampler);
        equirect_set.write(equirect_info, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, 0);
        VkDescriptorImageInfo env_info = environment_cube.get_descriptor_image_info(*sampler);
        env_set.write(env_info, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, 0);

        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        {
            bake_environment_cube(command_buffer);
            bake_irradiance_cube(command_buffer);
            bake_prefiltered_cube(command_buffer);
            bake_brdf_lut(command_buffer);
        }
        command_buffer.end();
        VulkanCommand::get_singleton().execute_command_buffer_graphics(command_buffer);

        const auto end = std::chrono::high_resolution_clock::now();
        bake_ms = std::chrono::duration<double, std::milli>(end - start).count();
        outstream << std::format("[ PbrIbl ] IBL bake ({}): env {}²x{}mip, irradiance {}², prefiltered {}²x{}mip, LUT {}² → {:.1f} ms\n",
                                 environment_source, k_env_size, k_env_mips, k_irradiance_size,
                                 k_prefilter_size, k_prefilter_mips, k_lut_size, bake_ms);
    }

    void create_brdf_lut_image() {
        VkImageCreateInfo create_info = {
            .imageType = VK_IMAGE_TYPE_2D,
            .format = k_lut_format,
            .extent = { k_lut_size, k_lut_size, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        };
        lut_image.create(create_info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        lut_view.create(lut_image.Image(), VK_IMAGE_VIEW_TYPE_2D, k_lut_format, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });
    }

    void bake_environment_cube(VkCommandBuffer cmd) {
        // equirect HDR → cube（只画 mip 0，其余 mip 用 blit 链生成）
        for (uint32_t face = 0; face < 6; ++face) {
            const CubePushConstants push_constants{ .face = face };
            render_cube_face(cmd, environment_cube, equirect_pipeline, cube_bake_layout, equirect_set, face, 0, push_constants);
        }
        // mip 0 全部面 → TRANSFER_SRC，再生成 mip 链（最后统一过渡到 SHADER_READ_ONLY）
        transition_image(cmd, environment_cube.get_image(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         0, 1, 0, 6, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        image_operation::cmd_generate_mipmap2d(cmd, environment_cube.get_image(),
            VkExtent2D{ k_env_size, k_env_size }, k_env_mips, 6,
            { VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
            VK_FILTER_LINEAR);
    }

    void bake_irradiance_cube(VkCommandBuffer cmd) {
        const CubePushConstants push_constants{
            .param0 = 2.0f * 3.14159265359f / 90.0f,        // deltaPhi
            .param1 = 0.5f * 3.14159265359f / 32.0f         // deltaTheta
        };
        for (uint32_t face = 0; face < 6; ++face) {
            CubePushConstants per_face = push_constants;
            per_face.face = face;
            render_cube_face(cmd, irradiance_cube, irradiance_pipeline, cube_bake_layout, env_set, face, 0, per_face);
        }
        transition_image(cmd, irradiance_cube.get_image(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         0, 1, 0, 6, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    void bake_prefiltered_cube(VkCommandBuffer cmd) {
        for (uint32_t mip = 0; mip < k_prefilter_mips; ++mip) {
            const float roughness = static_cast<float>(mip) / static_cast<float>(k_prefilter_mips - 1);
            for (uint32_t face = 0; face < 6; ++face) {
                CubePushConstants push_constants{
                    .face = face,
                    .param0 = roughness,
                    .param1 = static_cast<float>(k_prefilter_samples)
                };
                render_cube_face(cmd, prefiltered_cube, prefilter_pipeline, cube_bake_layout, env_set, face, mip, push_constants);
            }
        }
        transition_image(cmd, prefiltered_cube.get_image(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         0, k_prefilter_mips, 0, 6, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    void bake_brdf_lut(VkCommandBuffer cmd) {
        transition_image(cmd, lut_image.Image(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         0, 1, 0, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo attachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = lut_view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE
        };
        VkRenderingInfo rendering_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { { 0, 0 }, { k_lut_size, k_lut_size } },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment
        };
        vkCmdBeginRendering(cmd, &rendering_info);
        set_dynamic_viewport(cmd, VkExtent2D{ k_lut_size, k_lut_size });
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lut_pipeline);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);

        transition_image(cmd, lut_image.Image(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         0, 1, 0, 1, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    void render_cube_face(VkCommandBuffer cmd, VulkanTextureCube& cube, VulkanPipeline& pipeline, VulkanPipelineLayout& layout,
                          VulkanDescriptorSet& descriptor_set, uint32_t face, uint32_t mip, const CubePushConstants& push_constants) {
        const uint32_t size = cube.get_face_size(mip);
        transition_image(cmd, cube.get_image(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         mip, 1, face, 1, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

        VkRenderingAttachmentInfo attachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = cube.get_face_view(face, mip),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE
        };
        VkRenderingInfo rendering_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { { 0, 0 }, { size, size } },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &attachment
        };
        vkCmdBeginRendering(cmd, &rendering_info);
        set_dynamic_viewport(cmd, VkExtent2D{ size, size });
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, descriptor_set.Address(), 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CubePushConstants), &push_constants);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
    }

    static void set_dynamic_viewport(VkCommandBuffer cmd, VkExtent2D extent) {
        const VkViewport viewport = {
            .x = 0.0f, .y = 0.0f,
            .width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height),
            .minDepth = 0.0f, .maxDepth = 1.0f
        };
        const VkRect2D scissor = { { 0, 0 }, extent };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    static void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
                                 uint32_t mip, uint32_t mip_count, uint32_t layer, uint32_t layer_count,
                                 VkPipelineStageFlags src_stage, VkAccessFlags src_access,
                                 VkPipelineStageFlags dst_stage, VkAccessFlags dst_access) {
        VkImageMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = src_access,
            .dstAccessMask = dst_access,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, mip, mip_count, layer, layer_count }
        };
        vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // ---------------------------------------------------------------- 资源创建

    bool load_equirect_hdr() {
        const std::filesystem::path hdr_path = G_PROJECT_ROOT / "Assets/benchmark/Bistro/Bistro_v5_2/san_giuseppe_bridge_4k.hdr";
        std::vector<uint16_t> half_pixels;
        uint32_t width = 0, height = 0;
        std::string error;
        if (!std::filesystem::exists(hdr_path)) {
            outstream << std::format("[ PbrIbl ] 找不到 {}，退回程序化 test cube（先跑 scripts/fetch-benchmark-scenes.ps1 -Scene bistro）\n",
                                     hdr_path.string());
            return false;
        }
        if (!HdrImage::load(hdr_path, half_pixels, width, height, error)) {
            outstream << std::format("[ PbrIbl ] HDR 加载失败：{}\n", error);
            return false;
        }
        equirect_hdr.create(HdrImage::as_bytes(half_pixels), VkExtent2D{ width, height }, k_hdr_format, k_hdr_format, true);
        environment_source = std::format("{} ({}x{})", hdr_path.filename().string(), width, height);
        return true;
    }

    bool create_bake_resources() {
        load_equirect_hdr();   // 失败时 environment_source 保持 (none)，bake 阶段会用程序化 pattern 兜底

        VkDescriptorSetLayoutBinding binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        };
        VkDescriptorSetLayoutCreateInfo set_layout_info = { .bindingCount = 1, .pBindings = &binding };
        if (bake_set_layout.create(set_layout_info) != VK_SUCCESS)
            return false;

        VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 } };
        bake_descriptor_pool = std::make_unique<VulkanDescriptorPool>(2, pool_sizes);
        if (bake_descriptor_pool->allocate_sets(equirect_set, bake_set_layout) != VK_SUCCESS ||
            bake_descriptor_pool->allocate_sets(env_set, bake_set_layout) != VK_SUCCESS)
            return false;

        VkPushConstantRange push_constant_range = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = sizeof(CubePushConstants)
        };
        VkPipelineLayoutCreateInfo cube_layout_info = {
            .setLayoutCount = 1,
            .pSetLayouts = bake_set_layout.Address(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range
        };
        if (cube_bake_layout.create(cube_layout_info) != VK_SUCCESS)
            return false;

        VkPipelineLayoutCreateInfo lut_layout_info = {};
        if (lut_layout.create(lut_layout_info) != VK_SUCCESS)
            return false;

        if (!create_bake_pipeline(equirect_pipeline, cube_bake_layout, "BasicRendering/pbrIbl/cubeface.vert.spv",
                                  "BasicRendering/pbrIbl/equirect_to_cube.frag.spv", k_hdr_format))
            return false;
        if (!create_bake_pipeline(irradiance_pipeline, cube_bake_layout, "BasicRendering/pbrIbl/cubeface.vert.spv",
                                  "BasicRendering/pbrIbl/irradiance_cube.frag.spv", k_hdr_format))
            return false;
        if (!create_bake_pipeline(prefilter_pipeline, cube_bake_layout, "BasicRendering/pbrIbl/cubeface.vert.spv",
                                  "BasicRendering/pbrIbl/prefilter_env.frag.spv", k_hdr_format))
            return false;
        if (!create_bake_pipeline(lut_pipeline, lut_layout, "BasicRendering/pbrIbl/brdf_lut.vert.spv",
                                  "BasicRendering/pbrIbl/brdf_lut.frag.spv", k_lut_format))
            return false;
        return true;
    }

    static bool create_bake_pipeline(VulkanPipeline& pipeline, VulkanPipelineLayout& layout,
                                     const char* vert_path, const char* frag_path, VkFormat color_format) {
        VulkanShaderModule vert(get_shader_path(vert_path).string().c_str());
        VulkanShaderModule frag(get_shader_path(frag_path).string().c_str());
        VkPipelineShaderStageCreateInfo shader_stages[2] = {
            vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        VkPipelineRenderingCreateInfo rendering_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachmentFormats = &color_format;

        GraphicsPipelineCreateInfoPack pack;
        pack.create_info.layout = layout;
        pack.create_info.renderPass = VK_NULL_HANDLE;
        pack.create_info.pNext = &rendering_info;
        pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pack.rasterization_state_create_info.cullMode = VK_CULL_MODE_NONE;
        pack.rasterization_state_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        pack.depth_stencil_state_create_info.depthTestEnable = VK_FALSE;
        pack.depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
        pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
        // bake 的目标尺寸随面/mip 变化 → 用动态 viewport/scissor（不往 vector 里塞静态值）
        pack.dynamic_states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
        pack.dynamic_states.push_back(VK_DYNAMIC_STATE_SCISSOR);
        pack.update_all_arrays();
        pack.create_info.stageCount = 2;
        pack.create_info.pStages = shader_stages;
        return pipeline.create(pack) == VK_SUCCESS;
    }

    // ---------------------------------------------------------------- skybox（M1.1 保留）

    bool create_skybox_resources() {
        VkDescriptorSetLayoutBinding binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        };
        VkDescriptorSetLayoutCreateInfo layout_create_info = { .bindingCount = 1, .pBindings = &binding };
        if (descriptor_set_layout.create(layout_create_info) != VK_SUCCESS)
            return false;
        VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } };
        descriptor_pool = std::make_unique<VulkanDescriptorPool>(1, pool_sizes);
        skybox_descriptor_set = std::make_unique<VulkanDescriptorSet>();
        if (descriptor_pool->allocate_sets(*skybox_descriptor_set, descriptor_set_layout) != VK_SUCCESS)
            return false;

        VkPushConstantRange push_constant_range = {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(SkyboxPushConstants)
        };
        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
            .setLayoutCount = 1,
            .pSetLayouts = descriptor_set_layout.Address(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range
        };
        if (skybox_pipeline_layout.create(pipeline_layout_create_info) != VK_SUCCESS)
            return false;
        return create_skybox_pipeline();
    }

    void create_environment_cube_descriptor() {
        // 环境 cube 的 descriptor 必须在 bake 完成（且过渡到 SHADER_READ_ONLY）之后写
        VkDescriptorImageInfo image_info = environment_cube.get_descriptor_image_info(*sampler);
        skybox_descriptor_set->write(image_info, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0, 0);
    }

    struct SkyboxPushConstants {
        glm::mat4 inv_view_projection;
        glm::vec4 camera_position;
    };

    bool create_skybox_pipeline() {
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
            pack.create_info.layout = skybox_pipeline_layout;
            pack.create_info.renderPass = VK_NULL_HANDLE;
            pack.create_info.pNext = &rendering_info;
            pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            pack.viewports.emplace_back(0.f, 0.f, float(window_size.width), float(window_size.height), 0.f, 1.f);
            pack.scissors.emplace_back(VkOffset2D{}, window_size);
            pack.rasterization_state_create_info.cullMode = VK_CULL_MODE_NONE;
            pack.rasterization_state_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            pack.depth_stencil_state_create_info.depthTestEnable = VK_FALSE;
            pack.depth_stencil_state_create_info.depthWriteEnable = VK_FALSE;
            pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
            pack.update_all_arrays();
            pack.create_info.stageCount = 2;
            pack.create_info.pStages = shader_stages;
            return skybox_pipeline.create(pack) == VK_SUCCESS;
        };
        auto destroy = [this] {
            if (current_demo_name != get_type())
                return;
            skybox_pipeline.~VulkanPipeline();
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
        const SkyboxPushConstants push_constants{
            .inv_view_projection = glm::inverse(view_projection),
            .camera_position = glm::vec4(glm::vec3(glm::inverse(camera.matrices.view) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)), 1.0f)
        };

        vkCmdBeginRendering(cmd, &target->info);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skybox_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skybox_pipeline_layout, 0, 1,
                                skybox_descriptor_set->Address(), 0, nullptr);
        vkCmdPushConstants(cmd, skybox_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SkyboxPushConstants), &push_constants);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRendering(cmd);
    }

    void initialize_camera() {
        camera.flip_y = true;
        camera.set_perspective(60.0f, (float)window_size.width / (float)window_size.height, 0.1f, 256.0f);
        camera.set_rotation({ 0.0f, 30.0f, 0.0f });
        camera.set_position({ 0.0f, 0.0f, -1.0f });
    }
};
