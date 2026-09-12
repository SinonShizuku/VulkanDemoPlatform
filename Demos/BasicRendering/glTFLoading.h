#pragma once
#include "../DemoBase3D.h"
#include "../../Geometry/Vertex.h"
#include "../../Geometry/Model.h"

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
            /*externally_synchronized=*/true);

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
            pipeline_create_info_pack.create_info.renderPass = VulkanPipelineManager::get_singleton().get_rpwf_ds().render_pass;
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
        const framegraph::RenderTarget* target = executor_.acquire_render_target(frame_graph_, attachments);
        if (!target) {
            outstream << std::format("[ glTFLoading ] 获取 render target 失败: {}\n", executor_.get_error());
            return;
        }

        VkClearValue clear_values[2] = {
            {.color = { 1.f, 1.f, 1.f, 1.f }},
            {.depthStencil = { 1.f, 0 }}
        };
        VkRenderPassBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin_info.renderPass = target->render_pass;
        begin_info.framebuffer = target->framebuffer;
        begin_info.renderArea = VkRect2D{ {}, target->extent };
        begin_info.clearValueCount = 2;
        begin_info.pClearValues = clear_values;

        vkCmdBeginRenderPass(cmd, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, descriptor_set->Address(), 0, nullptr);
        draw(gltf_model);
        vkCmdEndRenderPass(cmd);
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
            gltf_model.load_images(gltf_input);
            gltf_model.load_materials(gltf_input);
            gltf_model.load_textures(gltf_input);
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

        size_t vertex_buffer_size = vertex_buffer.size() * sizeof(VulkanglTFModel::Vertex);
        size_t index_buffer_size = index_buffer.size() * sizeof(uint32_t);
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

    void load_assets() {
        auto model_path = G_PROJECT_ROOT / "Assets/models/FlightHelmet/glTF/FlightHelmet.gltf";
        load_glTF_file(model_path.string());
    }

    void initialize_camera() {
        camera.flip_y = true;
        camera.set_perspective(60.0f, (float)window_size.width / (float)window_size.height, 0.1f, 256.0f);
        camera.set_rotation({ 45.0f, 0.0f, 0.0f });
        camera.set_position({ 0.0f, -0.1f, -1.0f });
    }
};
