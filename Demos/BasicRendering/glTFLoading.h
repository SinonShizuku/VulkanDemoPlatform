#pragma once
#include "../DemoBase3D.h"
#include "../../Geometry/Vertex.h"
#include "../../Geometry/Model.h"
#include "../../Geometry/AssimpModelLoader.h"

#include "../../VulkanBase/components/VulkanTexture.h"
#include "../../VulkanBase/components/VulkanSampler.h"
#include "../../VulkanBase/components/VulkanMemory.h"
#include "../../VulkanBase/FrameGraph/FrameGraph.h"
#include "../../VulkanBase/FrameGraph/FrameGraphExecutor.h"

class glTFLoading : public DemoBase3D {
public:
    glTFLoading(GLFWwindow *window)
    : DemoBase3D("glTFLoading", DemoCategoryType::BASIC_RENDERING, "",  window)
    {}
    ~glTFLoading() override = default;

    bool initialize_scene_resources() override {
        allocate_command_buffer();
        load_assets();

        VkSamplerCreateInfo sampler_create_info = VulkanTexture2D::get_sampler_create_info();
        sampler = std::make_unique<VulkanSampler>(sampler_create_info);

        initialize_camera();
        register_glfw_callback();

        if (!create_descriptor_resources() ||
            !create_pipeline_layout() ||
            !create_pipeline()) {
            return false;
        }

        executor_.set_synchronization2(
            VulkanCore::get_singleton().get_vulkan_device().get_physical_device_vulkan13_features().synchronization2 == VK_TRUE);
        return true;
    }

    void cleanup_scene_resources() override {
        executor_.reset();
        // SharedResourceManager::get_singleton().get_shared_fence().wait_and_reset();
        // 清理资源
        descriptor_set.reset();
        descriptor_pool.reset();
        sampler.reset();

        // 清理管线
        pipeline.~VulkanPipeline();
        pipeline_layout.~VulkanPipelineLayout();
        descriptor_set_layout.~VulkanDescriptorSetLayout();

        // 清理回调
        clean_up_glfw_callback();

        free_command_buffer();
    }

    void render_frame() override {
        update_uniform_data();
        uniform_buffer->transfer_data(uniform_data);
        const uint32_t current_image_index = VulkanSwapchainManager::get_singleton().get_current_image_index();
        const auto& swapchain_info = VulkanSwapchainManager::get_singleton().get_swapchain_create_info();

        // 颜色 = 导入的 swapchain image（layout/同步交给 render pass，图不为它生成 barrier）；
        // 深度 = 图拥有的 transient 纹理（图的 barrier 负责它的 layout）。
        frame_graph_.reset();
        frame_graph_.set_name("glTFLoading");

        framegraph::TextureDesc color_desc;
        color_desc.name = "SwapchainImage";
        color_desc.format = swapchain_info.imageFormat;
        color_desc.extent = VkExtent3D{ swapchain_info.imageExtent.width, swapchain_info.imageExtent.height, 1 };
        color_desc.usage = framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::Present;
        const framegraph::ResourceHandle color = frame_graph_.import_texture(
            color_desc, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
            /*externally_synchronized=*/false);

        framegraph::TextureDesc depth_desc;
        depth_desc.name = "Depth";
        depth_desc.format = VulkanCore::get_singleton().get_vulkan_device().get_supported_depth_format();
        depth_desc.extent = VkExtent3D{ swapchain_info.imageExtent.width, swapchain_info.imageExtent.height, 1 };
        depth_desc.usage = framegraph::ImageUsage::DepthStencilAttachment | framegraph::ImageUsage::Sampled;
        const framegraph::ResourceHandle depth = frame_graph_.create_texture(depth_desc);

        frame_graph_.add_graphics_pass("Scene")
            .write(color, framegraph::usage::color_attachment_write())
            .write(depth, framegraph::usage::depth_stencil_write())
            .execute([this, color, depth](framegraph::PassContext& context) { record_scene_pass(context, color, depth); });
        // 呈现前的最终过渡：由图标出 COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
        frame_graph_.add_transfer_pass("Present")
            .write(color, framegraph::usage::present())
            .execute([](framegraph::PassContext&) {});

        if (!frame_graph_.compile()) {
            outstream << std::format("[ glTFLoading ] compile 失败: {}\n", frame_graph_.get_error());
            return;
        }
        executor_.import_texture(color,
                                 VulkanSwapchainManager::get_singleton().get_swapchain_image(current_image_index),
                                 VulkanSwapchainManager::get_singleton().get_swapchain_image_view(current_image_index));
        if (!executor_.prepare(frame_graph_)) {
            outstream << std::format("[ glTFLoading ] prepare 失败: {}\n", executor_.get_error());
            return;
        }

        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        {
            executor_.execute(frame_graph_, command_buffer);

            VkClearValue clear_values[2] = {
                {.color = { 1.f, 1.f, 1.f, 1.f }},
                {.depthStencil = { 1.f, 0 }}
            };
            imgui_render(current_image_index, clear_values);
        }
        command_buffer.end();
    }



private:
    framegraph::FrameGraph frame_graph_;
    framegraph::FrameGraphExecutor executor_;

    bool wireframe = false;
    std::unique_ptr<VulkanSampler> sampler;
    std::unique_ptr<VulkanDescriptorPool> descriptor_pool;
    std::unique_ptr<VulkanDescriptorSet> descriptor_set;
    struct DescriptorSetLayouts {
        VulkanDescriptorSetLayout matrices;
        VulkanDescriptorSetLayout textures;
    } descriptor_set_layouts;
    std::unique_ptr<VulkanUniformBuffer> uniform_buffer;

    // 线框模式
    // VulkanDescriptorSetLayout descriptor_set_layout_wireframe;
    VulkanPipeline pipeline_wireframe;

    VulkanglTFModel gltf_model;
    std::string loaded_scene_name;
    glm::vec3 scene_bounds_min = glm::vec3(0.0f);
    glm::vec3 scene_bounds_max = glm::vec3(0.0f);
    bool has_scene_bounds = false;

    // struct UniformData {
    //     glm::mat4 projection = flip_vertical(glm::perspective(glm::radians(60.0f), (float)window_size.width / (float)window_size.height, 0.1f, 256.0f));
    //     glm::mat4 model;
    //     glm::vec4 light_pos = glm::vec4(5.0f, 5.0f, -5.0f, 1.0f);
    //     glm::vec4 view_pos;
    // } uniform_data;

    void update_uniform_data() {
        uniform_data.projection = camera.matrices.perspective;
        uniform_data.model = camera.matrices.view;
        uniform_data.view_pos = camera.view_pos;
    }


    bool create_pipeline_layout() {
        std::array<VkDescriptorSetLayout, 2> set_layouts = { descriptor_set_layouts.matrices, descriptor_set_layouts.textures };
        VkPushConstantRange push_constant_range = {
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(glm::mat4)
        };
        VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
            .setLayoutCount = static_cast<uint32_t>(set_layouts.size()),
            .pSetLayouts = set_layouts.data(),
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range
        };
        return pipeline_layout.create(pipeline_layout_create_info) == VK_SUCCESS;
    }

    bool create_pipeline() {
        static VulkanShaderModule vert(get_shader_path("BasicRendering/gltfLoading/gltfLoading.vert.spv").string().c_str());
        static VulkanShaderModule frag(get_shader_path("BasicRendering/gltfLoading/gltfLoading.frag.spv").string().c_str());
        static VkPipelineShaderStageCreateInfo shader_stage_create_infos_texture[2] = {
            vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        auto create = [&] {
            if (current_demo_name != get_type()) return false;
            GraphicsPipelineCreateInfoPack pipeline_create_info_pack;
            pipeline_create_info_pack.create_info.layout = pipeline_layout;
            // dynamic rendering：不再绑定 render pass，改用 VkPipelineRenderingCreateInfo 声明附件格式
            const VkFormat color_format = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat;
            VkPipelineRenderingCreateInfo rendering_create_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
            rendering_create_info.colorAttachmentCount = 1;
            rendering_create_info.pColorAttachmentFormats = &color_format;
            rendering_create_info.depthAttachmentFormat = VulkanCore::get_singleton().get_vulkan_device().get_supported_depth_format();
            pipeline_create_info_pack.create_info.renderPass = VK_NULL_HANDLE;
            pipeline_create_info_pack.create_info.pNext = &rendering_create_info;
            // 子通道只有一个，pipeline_create_info_pack.createInfo.renderPass使用默认值0

            // vertex buffer
            //数据来自0号顶点缓冲区，输入频率是逐顶点输入
            pipeline_create_info_pack.vertex_input_bindings.emplace_back(0, sizeof(VulkanglTFModel::Vertex), VK_VERTEX_INPUT_RATE_VERTEX);
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, pos));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, normal));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, uv));
            pipeline_create_info_pack.vertex_input_attributes.emplace_back(3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, color));

            // pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            pipeline_create_info_pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            pipeline_create_info_pack.viewports.emplace_back(0.f, 0.f, float(window_size.width), float(window_size.height), 0.f, 1.f);
            pipeline_create_info_pack.scissors.emplace_back(VkOffset2D{},window_size);
            pipeline_create_info_pack.rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
            pipeline_create_info_pack.rasterization_state_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            pipeline_create_info_pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            pipeline_create_info_pack.depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
            pipeline_create_info_pack.depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
            pipeline_create_info_pack.depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
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
            if (current_demo_name != get_type()) return;
            pipeline.~VulkanPipeline();
        };
        add_swapchain_create_callback(create);
        add_swapchain_destroy_callback(destroy);
        return create();
    }

    bool create_descriptor_resources() {
        VkDescriptorSetLayoutBinding descriptor_set_layout_binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
        };
        VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
            .bindingCount = 1,
            .pBindings = &descriptor_set_layout_binding
        };
        descriptor_set_layouts.matrices.create(descriptor_set_layout_create_info);
        descriptor_set_layout_binding = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
        };
        descriptor_set_layouts.textures.create(descriptor_set_layout_create_info);

        // glm::mat4 transM = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.1f, -1.0f));
        // glm::mat4 rotM = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        // uniform_data.model = transM * rotM;
        // uniform_data.view_pos = glm::vec4(0.0f, -0.1f, 1.0f, 0.0f);

        uniform_buffer = std::make_unique<VulkanUniformBuffer>(sizeof(uniform_data));
        // uniform_buffer->transfer_data(uniform_data);

        VkDescriptorBufferInfo buffer_info = {*uniform_buffer, 0, VK_WHOLE_SIZE};
        // 创建描述符池
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  static_cast<uint32_t>(gltf_model.images.size()) }
        };

        descriptor_pool = std::make_unique<VulkanDescriptorPool>(1 + gltf_model.images.size(), pool_sizes);
        // 分配描述符集
        descriptor_set = std::make_unique<VulkanDescriptorSet>();
        descriptor_pool->allocate_sets(*descriptor_set, descriptor_set_layouts.matrices);
        descriptor_set->write(buffer_info, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,0);

        for (auto& image : gltf_model.images) {
            VkDescriptorImageInfo image_info = {
                .sampler = *sampler,
                .imageView = image.texture.get_image_view(),
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            };
            descriptor_pool->allocate_sets(image.descriptor_set, descriptor_set_layouts.textures);
            image.descriptor_set.write(image_info, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }

        return true;
    }

    void draw_node(VulkanglTFModel &model, VulkanglTFModel::Node* node) {
        if (!node->mesh.primitives.empty()) {
            glm::mat4 node_matrix = node->matrix;
            VulkanglTFModel::Node* current_parent = node->parent;
            while (current_parent) {
                node_matrix = current_parent->matrix * node_matrix;
                current_parent = current_parent->parent;
            }
            vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &node_matrix);
            for (VulkanglTFModel::Primitive& primitive : node->mesh.primitives) {
                if (primitive.index_count > 0) {
                    auto i = primitive.material_index;
                    VulkanglTFModel::Texture texture = model.textures[model.materials[i].base_color_texture_index];
                    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 1, 1, model.images[texture.image_index].descriptor_set.Address(), 0, nullptr);
                    vkCmdDrawIndexed(command_buffer, primitive.index_count, 1, primitive.first_index, 0, 0);
                }
            }
        }
        for (auto& child : node->children) {
            draw_node(model, child);
        }
    }

    // pass 主体：executor 提供与现有管线兼容的 render pass/framebuffer。
    // 颜色附件由 render pass 完成 UNDEFINED -> COLOR_ATTACHMENT -> PRESENT；
    // 深度附件由图规划并录制 barrier。
    void record_scene_pass(framegraph::PassContext& context, framegraph::ResourceHandle color, framegraph::ResourceHandle depth) {
        auto* frame = static_cast<framegraph::FrameGraphExecution*>(context.user_data);
        VkCommandBuffer cmd = frame->command_buffer;

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
            {.color = { 1.f, 1.f, 1.f, 1.f }},
            {.depthStencil = { 1.f, 0 }}
        };
        const framegraph::FrameGraphExecutor::DynamicRenderingTarget* target =
            executor_.acquire_rendering_info(frame_graph_, attachments, clear_values);
        if (!target) {
            outstream << std::format("[ glTFLoading ] 获取 rendering info 失败: {}\n", executor_.get_error());
            return;
        }

        vkCmdBeginRendering(cmd, &target->info);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, descriptor_set->Address(), 0, nullptr);
        draw(gltf_model);
        vkCmdEndRendering(cmd);
    }

    void draw(VulkanglTFModel &model) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command_buffer, 0, 1, model.vertices.Address(), &offset);
        vkCmdBindIndexBuffer(command_buffer, model.indices.index_buffer, 0, VK_INDEX_TYPE_UINT32);
        for (auto& node : model.nodes) {
            draw_node(model, node);
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
            // 没有贴图的模型（例如 TeapotsAndPillars）：补一张 1x1 白色贴图，
            // 否则材质的 base color texture 索引无效，绘制时会越界/绑不到 descriptor set。
            if (gltf_input.images.empty()) {
                tinygltf::Image white_image;
                white_image.width = 1;
                white_image.height = 1;
                white_image.component = 4;
                white_image.bits = 8;
                white_image.image = { 255, 255, 255, 255 };
                gltf_input.images.push_back(white_image);
                tinygltf::Texture white_texture;
                white_texture.source = 0;
                gltf_input.textures.push_back(white_texture);
            }
            gltf_model.load_images(gltf_input);
            gltf_model.load_materials(gltf_input);
            gltf_model.load_textures(gltf_input);
            for (auto& material : gltf_model.materials) {
                if (material.base_color_texture_index < 0 ||
                    material.base_color_texture_index >= static_cast<int>(gltf_model.textures.size())) {
                    material.base_color_texture_index = 0;  // 无贴图材质回落到白色贴图
                }
            }
            const tinygltf::Scene& scene = gltf_input.scenes[0];
            for (int n : scene.nodes) {
                const tinygltf::Node node = gltf_input.nodes[n];
                gltf_model.load_node(node, gltf_input, nullptr, index_buffer, vertex_buffer);
            }
        }
        else {
            outstream << std::format("[ Model ] Could not open the glTF file.\nMake sure the assets submodule has been checked out and is up-to-date.\n");
            return;
        }

        upload_model_buffers(vertex_buffer, index_buffer);
        loaded_scene_draw_calls = count_draw_calls();
    }

    // 顶点/索引上传：tinygltf 与 assimp 两条加载路径共用。
    void upload_model_buffers(const std::vector<VulkanglTFModel::Vertex>& vertex_buffer,
                              const std::vector<uint32_t>& index_buffer) {
        const size_t vertex_buffer_size = vertex_buffer.size() * sizeof(VulkanglTFModel::Vertex);
        const size_t index_buffer_size = index_buffer.size() * sizeof(uint32_t);
        gltf_model.indices.count = static_cast<uint32_t>(index_buffer.size());

        if (vertex_buffer_size > 0) {
            gltf_model.vertices.create(vertex_buffer_size);
            gltf_model.vertices.transfer_data(vertex_buffer.data(), vertex_buffer_size);
        }
        if (index_buffer_size > 0) {
            gltf_model.indices.index_buffer.create(index_buffer_size);
            gltf_model.indices.index_buffer.transfer_data(index_buffer.data(), index_buffer_size);
        }
    }

    // §14.5 第二阶段：FBX/OBJ/PLY 走 assimp，映射到同一个 VulkanglTFModel，复用现有 descriptor/绘制流程。
    void load_assimp_file(const std::filesystem::path& filename) {
        AssimpModelLoader::Options options;
        AssimpModelLoader::Stats stats;
        std::string error;
        std::vector<VulkanglTFModel::Vertex> vertex_buffer;
        std::vector<uint32_t> index_buffer;
        if (!AssimpModelLoader::load(filename, gltf_model, vertex_buffer, index_buffer, options, stats, error)) {
            outstream << std::format("[ Model ] 打不开 {}（assimp）：{}\n", filename.string(), error);
            return;
        }
        upload_model_buffers(vertex_buffer, index_buffer);
        loaded_scene_draw_calls = count_draw_calls();

        outstream << std::format(
            "[ Model ] assimp: {} | mesh={} primitive={} vertex={} index={} material={} texture={} dds={} hdr={} skipped_texture={}\n",
            filename.filename().string(), stats.mesh_count, stats.primitive_count, stats.vertex_count,
            stats.index_count, stats.material_count, stats.texture_count, stats.dds_texture_count,
            stats.hdr_texture_count, stats.skipped_texture_count);
        outstream << std::format(
            "[ Model ] slots: base={} diffuse={} normal={} metallic={} roughness={} emissive={} ao={} specular={} glossiness={} lightmap={} reflection={} unknown={}\n",
            stats.slots.base_color, stats.slots.diffuse, stats.slots.normal, stats.slots.metallic,
            stats.slots.roughness, stats.slots.emissive, stats.slots.ambient_occlusion, stats.slots.specular,
            stats.slots.glossiness, stats.slots.lightmap, stats.slots.reflection, stats.slots.unknown);
        for (const std::string& warning : stats.warnings)
            outstream << std::format("[ Model ] WARN {}\n", warning);

        // 包围盒先存下来：initialize_camera() 在 load_assets() 之后才跑，那里再做实际取景，
        // 否则固定机位会被默认值覆盖（踩过一次）。
        if (stats.has_bounds) {
            scene_bounds_min = stats.bounds_min;
            scene_bounds_max = stats.bounds_max;
            has_scene_bounds = true;
        }
    }

    // 每帧的 draw call 数 = primitive 数（当前实现逐图元一次 bind + draw），写进 benchmark 元数据。
    uint32_t count_draw_calls() const {
        uint32_t count = 0;
        for (const VulkanglTFModel::Node* node : gltf_model.nodes)
            count += count_node_draw_calls(node);
        return count;
    }

    static uint32_t count_node_draw_calls(const VulkanglTFModel::Node* node) {
        uint32_t count = static_cast<uint32_t>(node->mesh.primitives.size());
        for (const VulkanglTFModel::Node* child : node->children)
            count += count_node_draw_calls(child);
        return count;
    }

    // 由包围盒推一个固定机位：方位角固定（沿 -Z 看）、俯视 20°，距离由包围球与 FOV 推出。
    // Camera 的约定是 view = translate(position) * rotate，且 flip_y 会把 translation.y 再取反，
    // 所以 position 字段不等于相机世界坐标：必须按 t = -R * camera_position 回填。
    void frame_camera_on_bounds(const glm::vec3& bounds_min, const glm::vec3& bounds_max) {
        const glm::vec3 center = (bounds_min + bounds_max) * 0.5f;
        const float radius = std::max(glm::length(bounds_max - bounds_min) * 0.5f, 1e-3f);
        const float elevation_deg = -20.0f;
        const float elevation = glm::radians(elevation_deg);
        const glm::vec3 direction{ 0.0f, std::sin(elevation), -std::cos(elevation) };
        const float fov_deg = 60.0f;
        const float distance = radius / std::sin(glm::radians(fov_deg * 0.5f)) * 1.1f;
        const glm::vec3 camera_position = center - direction * distance;

        camera.set_perspective(fov_deg, (float)window_size.width / (float)window_size.height,
                               std::max(radius * 0.01f, 0.01f), distance + radius * 4.0f);
        camera.set_rotation({ elevation_deg, 0.0f, 0.0f });
        const glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), -elevation, glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 translation = -glm::vec3(rotation * glm::vec4(camera_position, 0.0f));
        camera.set_position({ translation.x, -translation.y, translation.z });
    }

    void load_assets() {
        // --scene 指定时优先；否则用默认 FlightHelmet。
        auto model_path = G_PROJECT_ROOT / "Assets/models/FlightHelmet/glTF/FlightHelmet.gltf";
        if (!command_line_scene.empty())
            model_path = resolve_scene_asset(command_line_scene);
        loaded_scene_name = model_path.filename().string();
        loaded_scene_asset = model_path.string();
        // §14.5：按扩展名分派——.fbx/.obj/.ply 走 assimp，.gltf/.glb 继续走 tinygltf。
        if (AssimpModelLoader::supports(model_path))
            load_assimp_file(model_path);
        else
            load_glTF_file(model_path.string());
    }

    void initialize_camera() {
        camera.flip_y = true;
        camera.set_perspective(60.0f, (float)window_size.width / (float)window_size.height, 0.1f, 256.0f);
        camera.set_rotation({ 45.0f, 0.0f, 0.0f });
        camera.set_position({ 0.0f, -0.1f, -1.0f });

        // §14.1.1 的相机策略：assimp 路径（FBX/OBJ/PLY）的尺寸跨好几个数量级，
        // 默认机位只适合随仓库自带的小模型，所以有包围盒时改用它推一个确定性固定机位。
        // 显式命名机位（bistro_view_0/1/2）与 --camera-path 是后续项。
        if (has_scene_bounds)
            frame_camera_on_bounds(scene_bounds_min, scene_bounds_max);
    }
};
