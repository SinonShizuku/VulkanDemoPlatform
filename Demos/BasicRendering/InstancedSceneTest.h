#pragma once
#include "../DemoBase3D.h"
#include "../../Geometry/Vertex.h"
#include "../../Geometry/Model.h"
#include "../../Geometry/AssimpModelLoader.h"
#include "../../VulkanBase/components/VulkanMemory.h"
#include "../../VulkanBase/FrameGraph/FrameGraph.h"
#include "../../VulkanBase/FrameGraph/FrameGraphExecutor.h"

// 实例化基准场景（§14.1 的 sponza_instanced_100k 预设载体）：
//   * 资产由 --scene 指定（默认 Sponza，其次 FlightHelmet）；
//   * 取资产里 index 数最少的图元做实例源，按 --instances（默认 100000）铺成网格；
//   * 模型矩阵走 instance-rate 顶点属性（baseline）；后续 GPU-driven 会换成 storage buffer + culling + indirect draw；
//   * 相机固定（benchmark 需要确定性），走 FrameGraph + dynamic rendering。
class InstancedSceneTest : public DemoBase3D {
public:
    InstancedSceneTest(GLFWwindow* window)
        : DemoBase3D("InstancedScene", DemoCategoryType::BASIC_RENDERING, "", window) {}
    ~InstancedSceneTest() override = default;

    bool initialize_scene_resources() override {
        allocate_command_buffer();
        load_assets();
        if (demo_scene.indices.count == 0 || instance_count_ == 0) {
            outstream << "[ InstancedScene ] 没有可实例化的图元\\n";
            return false;
        }
        build_instances();
        if (!create_pipeline_layout() || !create_descriptor_resources() || !create_pipeline())
            return false;
        return true;
    }

    void cleanup_scene_resources() override {
        executor_.reset();
        descriptor_set.~VulkanDescriptorSet();
        descriptor_pool.reset();
        uniform_buffer.reset();
        instance_buffer.reset();
        pipeline.~VulkanPipeline();
        pipeline_layout.~VulkanPipelineLayout();
        descriptor_set_layout.~VulkanDescriptorSetLayout();
        // 模型自带的 vertex / index buffer 也要在设备销毁前释放（否则 vkDestroyDevice 报 leaked objects）
        demo_scene.vertices.~VulkanVertexBuffer();
        demo_scene.indices.index_buffer.~VulkanIndexBuffer();
        free_command_buffer();
    }

    void render_frame() override {
        const auto current_image_index = VulkanSwapchainManager::get_singleton().get_current_image_index();
        const auto& swapchain_info = VulkanSwapchainManager::get_singleton().get_swapchain_create_info();

        frame_graph_.reset();
        frame_graph_.set_name("InstancedScene");

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
        depth_desc.extent = color_desc.extent;
        depth_desc.usage = framegraph::ImageUsage::DepthStencilAttachment;
        const framegraph::ResourceHandle depth = frame_graph_.create_texture(depth_desc);

        frame_graph_.add_graphics_pass("Scene")
            .write(color, framegraph::usage::color_attachment_write())
            .write(depth, framegraph::usage::depth_stencil_write())
            .execute([this, color, depth](framegraph::PassContext& context) { record_scene_pass(context, color, depth); });

        frame_graph_.add_transfer_pass("Present")
            .write(color, framegraph::usage::present())
            .execute([](framegraph::PassContext&) {});

        if (!frame_graph_.compile()) {
            outstream << std::format("[ InstancedScene ] compile 失败: {}\\n", frame_graph_.get_error());
            return;
        }
        executor_.import_texture(color,
                                 VulkanSwapchainManager::get_singleton().get_swapchain_image(current_image_index),
                                 VulkanSwapchainManager::get_singleton().get_swapchain_image_view(current_image_index));
        executor_.set_synchronization2(
            VulkanCore::get_singleton().get_vulkan_device().get_physical_device_vulkan13_features().synchronization2 == VK_TRUE);
        if (!executor_.prepare(frame_graph_)) {
            outstream << std::format("[ InstancedScene ] prepare 失败: {}\\n", executor_.get_error());
            return;
        }

        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        {
            executor_.execute(frame_graph_, command_buffer);
            VkClearValue clear_values[2] = {
                {.color = { 0.05f, 0.06f, 0.09f, 1.f }},
                {.depthStencil = { 1.f, 0 }}
            };
            imgui_render(current_image_index, clear_values);
        }
        command_buffer.end();
    }

    void draw_custom_ui() override {
        if (ImGui::Begin("Instanced scene")) {
            ImGui::Text("asset: %s", loaded_scene_name.c_str());
            ImGui::Text("instances: %u", instance_count_);
            ImGui::Text("source primitive indices: %u", source_index_count_);
            ImGui::Text("draw calls: 1");
        }
        ImGui::End();
    }

private:
    void record_scene_pass(framegraph::PassContext& context, framegraph::ResourceHandle color,
                           framegraph::ResourceHandle depth) {
        auto* frame = static_cast<framegraph::FrameGraphExecution*>(context.user_data);
        const VkCommandBuffer cmd = frame->command_buffer;

        const framegraph::RenderTargetAttachment attachments[2] = {
            { .resource = color,
              .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
            {.color = { 0.05f, 0.06f, 0.09f, 1.f }},
            {.depthStencil = { 1.f, 0 }}
        };
        const framegraph::FrameGraphExecutor::DynamicRenderingTarget* target =
            executor_.acquire_rendering_info(frame_graph_, attachments, clear_values);
        if (!target) {
            outstream << std::format("[ InstancedScene ] 获取 rendering info 失败: {}\\n", executor_.get_error());
            return;
        }

        vkCmdBeginRendering(cmd, &target->info);
        VkViewport viewport = {
            .width = static_cast<float>(window_size.width),
            .height = static_cast<float>(window_size.height),
            .minDepth = 0.f,
            .maxDepth = 1.f
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor = { .offset = {0, 0}, .extent = {window_size.width, window_size.height} };
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1,
                                descriptor_set.Address(), 0, nullptr);
        VkDeviceSize offsets[2] = { 0, 0 };
        VkBuffer buffers[2] = { *demo_scene.vertices.Address(), *instance_buffer->Address() };
        vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
        vkCmdBindIndexBuffer(cmd, demo_scene.indices.index_buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, source_index_count_, instance_count_, source_first_index_, 0, 0);
        vkCmdEndRendering(cmd);
    }

    void build_instances() {
        // 取 index 数最少的图元做实例源（保证 10 万实例的三角形总量可控），铺成方形网格。
        uint32_t best_count = UINT32_MAX;
        for (VulkanglTFModel::Node* node : demo_scene.nodes)
            collect_primitives(node, best_count);
        if (best_count == UINT32_MAX)
            return;

        const uint32_t side = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(instance_count_))));
        const float spacing = 6.0f;
        std::vector<glm::mat4> transforms(instance_count_);
        for (uint32_t i = 0; i < instance_count_; ++i) {
            const uint32_t x = i % side;
            const uint32_t z = i / side;
            const float px = (static_cast<float>(x) - static_cast<float>(side) * 0.5f) * spacing;
            const float pz = (static_cast<float>(z) - static_cast<float>(side) * 0.5f) * spacing;
            const float y = static_cast<float>((i * 2654435761u) % 7u) * 0.15f;
            const float angle = static_cast<float>((i * 40503u) % 360u) * 3.14159265f / 180.0f;
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(px, y, pz));
            model = glm::rotate(model, angle, glm::vec3(0.f, 1.f, 0.f));
            model = glm::scale(model, glm::vec3(1.0f));
            transforms[i] = model;
        }
        const VkDeviceSize bytes = transforms.size() * sizeof(glm::mat4);
        instance_buffer = std::make_unique<VulkanVertexBuffer>(bytes);
        instance_buffer->transfer_data(transforms.data(), bytes);
    }

    void collect_primitives(VulkanglTFModel::Node* node, uint32_t& best_count) {
        if (node == nullptr)
            return;
        for (const VulkanglTFModel::Primitive& primitive : node->mesh.primitives) {
            if (primitive.index_count >= 3 && primitive.index_count < best_count) {
                best_count = primitive.index_count;
                source_first_index_ = primitive.first_index;
                source_index_count_ = primitive.index_count;
            }
        }
        for (VulkanglTFModel::Node* child : node->children)
            collect_primitives(child, best_count);
    }

    bool create_pipeline_layout() {
        VkDescriptorSetLayoutBinding bindings[1] = {
            { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1,
              .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT }
        };
        VkDescriptorSetLayoutCreateInfo layout_info = { .bindingCount = 1, .pBindings = bindings };
        if (descriptor_set_layout.create(layout_info) != VK_SUCCESS)
            return false;
        VkPipelineLayoutCreateInfo pipeline_layout_info = {
            .setLayoutCount = 1,
            .pSetLayouts = descriptor_set_layout.Address()
        };
        return pipeline_layout.create(pipeline_layout_info) == VK_SUCCESS;
    }

    bool create_descriptor_resources() {
        VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 } };
        descriptor_pool = std::make_unique<VulkanDescriptorPool>(1, pool_sizes);
        if (descriptor_pool->allocate_sets(descriptor_set, descriptor_set_layout) != VK_SUCCESS)
            return false;

        uniform_buffer = std::make_unique<VulkanUniformBuffer>(sizeof(glm::mat4));
        const glm::mat4 view_projection = build_view_projection();
        uniform_buffer->transfer_data(view_projection);
        VkDescriptorBufferInfo buffer_info = { *uniform_buffer, 0, VK_WHOLE_SIZE };
        descriptor_set.write(buffer_info, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 0);
        return true;
    }

    static glm::mat4 build_view_projection() {
        const glm::mat4 view = glm::lookAt(glm::vec3(0.f, 260.f, 520.f), glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f));
        glm::mat4 projection = glm::perspective(glm::radians(60.f), 1920.f / 1080.f, 1.f, 4000.f);
        projection[1][1] *= -1.0f;  // Vulkan 的 Y 轴向下
        return projection * view;
    }

    bool create_pipeline() {
        static VulkanShaderModule vert(get_shader_path("Benchmark/instanced.vert.spv").string().c_str());
        static VulkanShaderModule frag(get_shader_path("Benchmark/instanced.frag.spv").string().c_str());
        static VkPipelineShaderStageCreateInfo shader_stages[2] = {
            vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };

        const VkFormat color_format = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat;
        VkPipelineRenderingCreateInfo rendering_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachmentFormats = &color_format;
        rendering_info.depthAttachmentFormat = VulkanCore::get_singleton().get_vulkan_device().get_supported_depth_format();

        GraphicsPipelineCreateInfoPack pack;
        pack.create_info.layout = pipeline_layout;
        pack.create_info.renderPass = VK_NULL_HANDLE;
        pack.create_info.pNext = &rendering_info;

        pack.vertex_input_bindings.emplace_back(0, sizeof(VulkanglTFModel::Vertex), VK_VERTEX_INPUT_RATE_VERTEX);
        pack.vertex_input_bindings.emplace_back(1, sizeof(glm::mat4), VK_VERTEX_INPUT_RATE_INSTANCE);
        pack.vertex_input_attributes.emplace_back(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, pos));
        pack.vertex_input_attributes.emplace_back(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VulkanglTFModel::Vertex, normal));
        pack.vertex_input_attributes.emplace_back(2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanglTFModel::Vertex, uv));
        pack.vertex_input_attributes.emplace_back(3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanglTFModel::Vertex, color));
        for (uint32_t row = 0; row < 4; ++row)
            pack.vertex_input_attributes.emplace_back(4 + row, 1, VK_FORMAT_R32G32B32A32_SFLOAT, row * sizeof(glm::vec4));

        pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        pack.viewports.emplace_back(0.f, 0.f, float(window_size.width), float(window_size.height), 0.f, 1.f);
        pack.scissors.emplace_back(VkOffset2D{}, window_size);
        pack.rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
        pack.rasterization_state_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        pack.depth_stencil_state_create_info.depthTestEnable = VK_TRUE;
        pack.depth_stencil_state_create_info.depthWriteEnable = VK_TRUE;
        pack.depth_stencil_state_create_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
        pack.update_all_arrays();
        pack.create_info.stageCount = 2;
        pack.create_info.pStages = shader_stages;
        return pipeline.create(pack) == VK_SUCCESS;
    }

    void load_assets() {
        auto model_path = G_PROJECT_ROOT / "Assets/benchmark/Sponza/glTF/Sponza.gltf";
        if (!std::filesystem::exists(model_path))
            model_path = G_PROJECT_ROOT / "Assets/models/FlightHelmet/glTF/FlightHelmet.gltf";
        if (!command_line_scene.empty())
            model_path = resolve_scene_asset(command_line_scene);
        loaded_scene_name = model_path.filename().string();
        loaded_scene_asset = model_path.string();

        // §14.5：按扩展名分派——.fbx/.obj/.ply 走 assimp，.gltf/.glb 继续走 tinygltf。
        if (AssimpModelLoader::supports(model_path))
            load_assimp_scene(model_path);
        else
            load_glTF_scene(model_path);

        instance_count_ = benchmark_instances > 0 ? static_cast<uint32_t>(benchmark_instances) : 100000u;
        loaded_scene_draw_calls = 1;    // 整个场景一次实例化 draw
    }

    void load_glTF_scene(const std::filesystem::path& model_path) {
        tinygltf::Model gltf_input;
        tinygltf::TinyGLTF gltf_context;
        std::string error, warning;
        const bool loaded = gltf_context.LoadASCIIFromFile(&gltf_input, &error, &warning, model_path.string());
        if (!loaded) {
            outstream << std::format("[ InstancedScene ] 打不开 glTF: {}\\n", model_path.string());
            return;
        }
        std::vector<uint32_t> index_buffer;
        std::vector<VulkanglTFModel::Vertex> vertex_buffer;
        if (gltf_input.scenes.empty())
            return;
        for (int n : gltf_input.scenes[0].nodes)
            demo_scene.load_node(gltf_input.nodes[n], gltf_input, nullptr, index_buffer, vertex_buffer);
        upload_model_buffers(vertex_buffer, index_buffer);
    }

    void load_assimp_scene(const std::filesystem::path& model_path) {
        AssimpModelLoader::Options options;
        options.load_textures = false;      // 实例化压测只用几何（loader 会把顶点色一并带上）
        AssimpModelLoader::Stats stats;
        std::string error;
        std::vector<uint32_t> index_buffer;
        std::vector<VulkanglTFModel::Vertex> vertex_buffer;
        if (!AssimpModelLoader::load(model_path, demo_scene, vertex_buffer, index_buffer, options, stats, error)) {
            outstream << std::format("[ InstancedScene ] 打不开 {}（assimp）：{}\n", model_path.string(), error);
            return;
        }
        upload_model_buffers(vertex_buffer, index_buffer);
        outstream << std::format("[ InstancedScene ] assimp: {} | mesh={} primitive={} vertex={} index={}\n",
                                 model_path.filename().string(), stats.mesh_count, stats.primitive_count,
                                 stats.vertex_count, stats.index_count);
    }

    // 顶点/索引上传：tinygltf 与 assimp 两条加载路径共用。
    void upload_model_buffers(const std::vector<VulkanglTFModel::Vertex>& vertex_buffer,
                              const std::vector<uint32_t>& index_buffer) {
        if (!vertex_buffer.empty()) {
            const size_t vertex_bytes = vertex_buffer.size() * sizeof(VulkanglTFModel::Vertex);
            demo_scene.vertices.create(vertex_bytes);
            demo_scene.vertices.transfer_data(vertex_buffer.data(), vertex_bytes);
        }
        if (!index_buffer.empty()) {
            const size_t index_bytes = index_buffer.size() * sizeof(uint32_t);
            demo_scene.indices.count = static_cast<int>(index_buffer.size());
            demo_scene.indices.index_buffer.create(index_bytes);
            demo_scene.indices.index_buffer.transfer_data(index_buffer.data(), index_bytes);
        }
    }

    VulkanglTFModel demo_scene;
    std::string loaded_scene_name;
    framegraph::FrameGraph frame_graph_;
    framegraph::FrameGraphExecutor executor_;
    std::unique_ptr<VulkanUniformBuffer> uniform_buffer;
    std::unique_ptr<VulkanVertexBuffer> instance_buffer;
    std::unique_ptr<VulkanDescriptorPool> descriptor_pool;
    VulkanDescriptorSet descriptor_set;
    VulkanDescriptorSetLayout descriptor_set_layout;
    uint32_t instance_count_ = 0;
    uint32_t source_first_index_ = 0;
    uint32_t source_index_count_ = 0;
};
