#pragma once
#include "../DemoBase.h"
#include "../../Geometry/Vertex.h"
#include "../../Start.h"

#include "../../VulkanBase/FrameGraph/FrameGraph.h"
#include "../../VulkanBase/FrameGraph/FrameGraphExecutor.h"
#include "../../VulkanBase/components/VulkanSampler.h"
#include "../../VulkanBase/components/VulkanMemory.h"

// OffScreenRenderingTest 的 FrameGraph 版本（第一份真正由 RenderGraph 驱动的 pass 组织）：
//   - 离屏画布作为 transient 纹理交给 FrameGraph 声明，executor 负责创建真实 VkImage / 内存 / view；
//   - UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL、COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
//     这两次 layout 转换由图编译出来、由 executor 录制，Demo 内部不再手写任何 barrier；
//   - 离屏 pass 用 dynamic rendering 记录，因此不需要 VkRenderPass 的隐式 layout 转换；
//   - 屏幕合成与 ImGui 仍走现有 screen render pass（后续切片再收进图里）；
//   - 已知问题：复用 legacy 的 CanvasToScreen 通道合成后画面为空白，尚未定位是
//     legacy 屏幕路径本身的问题还是本 demo 的参数问题，已在 Docs 中记录为待办。
class FrameGraphOffScreenTest : public DemoBase {
public:
    FrameGraphOffScreenTest()
        : DemoBase("FrameGraphOffScreenTest", DemoCategoryType::VULKAN_TESTS,
                   "OffScreenRenderingTest 的 FrameGraph 版本：离屏画布与 layout 转换由图自动生成"),
          push_constants_offscreen_({
              { static_cast<float>(default_window_size.width), static_cast<float>(default_window_size.height) },
              { { 0.f, 0.f }, { 0.f, 0.f } }
          }) {
    }

    ~FrameGraphOffScreenTest() override = default;

    bool initialize_scene_resources() override {
        allocate_command_buffer();
        if (!create_pipeline_layout()) { outstream << "[TEMP] init: layout failed" << std::endl; return false; }
        if (!create_pipeline()) { outstream << "[TEMP] init: pipeline failed" << std::endl; return false; }
        if (!create_pipeline_layout_offscreen()) { outstream << "[TEMP] init: layout offscreen failed" << std::endl; return false; }
        if (!create_pipeline_offscreen()) { outstream << "[TEMP] init: line pipeline failed" << std::endl; return false; }

        VkSamplerCreateInfo sampler_create_info = VulkanTexture2D::get_sampler_create_info();
        sampler_ = std::make_unique<VulkanSampler>(sampler_create_info);

        if (!create_descriptor_resources()) {
            outstream << "[TEMP] init: descriptor failed" << std::endl;
            return false;
        }

        // 只有设备确实启用了 synchronization2 时才走 vkCmdPipelineBarrier2
        executor_.set_synchronization2(
            VulkanCore::get_singleton().get_vulkan_device().get_physical_device_vulkan13_features().synchronization2 == VK_TRUE);
        return true;
    }

    void cleanup_scene_resources() override {
        executor_.reset();
        descriptor_set_.reset();
        descriptor_pool_.reset();
        sampler_.reset();

        pipeline_.~VulkanPipeline();
        pipeline_layout_.~VulkanPipelineLayout();
        descriptor_set_layout_.~VulkanDescriptorSetLayout();
        pipeline_line_.~VulkanPipeline();
        pipeline_layout_line_.~VulkanPipelineLayout();

        free_command_buffer();
    }

    void render_frame() override {
        const uint32_t image_index = VulkanSwapchainManager::get_singleton().get_current_image_index();
        const auto& swapchain_info = VulkanSwapchainManager::get_singleton().get_swapchain_create_info();

        // ---------------------------------------------------------- 1. 声明图
        frame_graph_.reset();
        frame_graph_.set_name("FrameGraphOffScreenTest");

        framegraph::TextureDesc canvas_desc;
        canvas_desc.name = "OffscreenCanvas";
        canvas_desc.format = swapchain_info.imageFormat;
        canvas_desc.extent = VkExtent3D{ swapchain_info.imageExtent.width, swapchain_info.imageExtent.height, 1 };
        canvas_desc.usage = framegraph::ImageUsage::ColorAttachment | framegraph::ImageUsage::Sampled;
        const framegraph::ResourceHandle canvas = frame_graph_.create_texture(canvas_desc);

        frame_graph_.add_graphics_pass("DrawCanvas")
            .write(canvas, framegraph::usage::color_attachment_write())
            .execute([this, canvas](framegraph::PassContext& context) { record_canvas_pass(context, canvas); });

        frame_graph_.add_graphics_pass("Composite")
            .read(canvas, framegraph::usage::sampled_read())
            .execute([this](framegraph::PassContext& context) { record_composite_pass(context); });

        // ---------------------------------------------------------- 2. 编译与落实
        if (!frame_graph_.compile()) {
            outstream << std::format("[ FrameGraphOffScreenTest ] compile 失败: {}\n", frame_graph_.get_error());
            return;
        }
        if (!executor_.prepare(frame_graph_)) {
            outstream << std::format("[ FrameGraphOffScreenTest ] prepare 失败: {}\n", executor_.get_error());
            return;
        }
        update_canvas_descriptor(canvas);

        // ---------------------------------------------------------- 3. 录制
        command_buffer.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        {
            executor_.execute(frame_graph_, command_buffer);
    
            VkClearValue clear_color = { .color = { 1.f, 1.f, 1.f, 1.f } };
            imgui_render(image_index, clear_color);
        }
        command_buffer.end();

        // 与 OffScreenRenderingTest 一致的画布交互：鼠标左键清屏，位置写入 push constants。
        // 注意：demo 由工厂函数创建，DemoBase::window 可能未被 set_window() 赋值，
        // 因此这里统一向 SharedResourceManager 取窗口。
        if (GLFWwindow* glfw_window = SharedResourceManager::get_singleton().get_window()) {
            glfwGetCursorPos(glfw_window, &mouse_x_, &mouse_y_);
            clear_canvas_ = glfwGetMouseButton(glfw_window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        }
        push_constants_offscreen_.offsets[canvas_index_ = !canvas_index_] = { mouse_x_, mouse_y_ };
    }

    void draw_custom_ui() override {
        if (ImGui::Begin("FrameGraph")) {
            ImGui::Text("barrier recording: %s", executor_.get_stats().synchronization2 ? "vkCmdPipelineBarrier2" : "vkCmdPipelineBarrier");
            ImGui::Text("image barriers (last frame): %u", executor_.get_stats().image_barriers);
            ImGui::Text("buffer barriers (last frame): %u", executor_.get_stats().buffer_barriers);
            ImGui::Text("graph passes: %u", static_cast<uint32_t>(frame_graph_.get_passes().size()));
            ImGui::Text("graph barriers: image=%u layout_transitions=%u elided=%u",
                        frame_graph_.get_stats().image_barriers,
                        frame_graph_.get_stats().layout_transitions,
                        frame_graph_.get_stats().elided_barriers);
            if (!frame_graph_.get_error().empty()) {
                ImGui::TextWrapped("error: %s", frame_graph_.get_error().c_str());
            }
        }
        ImGui::End();
    }

private:
    framegraph::FrameGraph frame_graph_;
    framegraph::FrameGraphExecutor executor_;

    std::unique_ptr<VulkanSampler> sampler_;
    std::unique_ptr<VulkanDescriptorPool> descriptor_pool_;
    std::unique_ptr<VulkanDescriptorSet> descriptor_set_;

    // 离屏 line pipeline（dynamic rendering，无 VkRenderPass）
    VulkanPipelineLayout pipeline_layout_line_;
    VulkanPipeline pipeline_line_;
    // 屏幕合成 pipeline（现有 screen render pass）
    VulkanPipelineLayout pipeline_layout_;
    VulkanPipeline pipeline_;
    VulkanDescriptorSetLayout descriptor_set_layout_;

    bool clear_canvas_ = true;
    bool canvas_index_ = false;
    double mouse_x_ = 0.0;
    double mouse_y_ = 0.0;

    struct {
        glm::vec2 viewportSize;
        glm::vec2 offsets[2];
    } push_constants_offscreen_;

    bool create_pipeline_layout_offscreen() {
        VkPushConstantRange push_constant_range = { VK_SHADER_STAGE_VERTEX_BIT, 0, 24 };
        VkPipelineLayoutCreateInfo create_info = {
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &push_constant_range,
        };
        return pipeline_layout_line_.create(create_info) == VK_SUCCESS;
    }

    bool create_pipeline_layout() {
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
        descriptor_set_layout_.create(layout_create_info);

        VkPushConstantRange push_constant_ranges[] = {
            { VK_SHADER_STAGE_VERTEX_BIT, 0, 16 },
            { VK_SHADER_STAGE_FRAGMENT_BIT, 8, 8 }
        };
        VkPipelineLayoutCreateInfo create_info = {
            .pushConstantRangeCount = 2,
            .pPushConstantRanges = push_constant_ranges,
        };
        create_info.setLayoutCount = 1;
        create_info.pSetLayouts = descriptor_set_layout_.Address();
        return pipeline_layout_.create(create_info) == VK_SUCCESS;
    }

    // 离屏 line pipeline：dynamic rendering（pNext = VkPipelineRenderingCreateInfo）
    bool create_pipeline_offscreen() {
        static VulkanShaderModule vert_offscreen(get_shader_path("VulkanTests/Line.vert.spv").string().c_str());
        static VulkanShaderModule frag_offscreen(get_shader_path("VulkanTests/Line.frag.spv").string().c_str());
        VkPipelineShaderStageCreateInfo shader_stages[2] = {
            vert_offscreen.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag_offscreen.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        const VkFormat canvas_format = VulkanSwapchainManager::get_singleton().get_swapchain_create_info().imageFormat;
        VkPipelineRenderingCreateInfo rendering_create_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
        rendering_create_info.colorAttachmentCount = 1;
        rendering_create_info.pColorAttachmentFormats = &canvas_format;

        GraphicsPipelineCreateInfoPack pack;
        pack.create_info.layout = pipeline_layout_line_;
        pack.create_info.renderPass = VK_NULL_HANDLE;      // dynamic rendering
        pack.create_info.pNext = &rendering_create_info;
        pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        pack.viewports.emplace_back(0.f, 0.f, float(window_size.width), float(window_size.height), 0.f, 1.f);
        pack.scissors.emplace_back(VkOffset2D{}, window_size);
        pack.rasterization_state_create_info.lineWidth = 1.f;
        pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
        pack.update_all_arrays();
        pack.create_info.stageCount = 2;
        pack.create_info.pStages = shader_stages;
        return pipeline_line_.create(pack) == VK_SUCCESS;
    }

    bool create_pipeline() {
        static VulkanShaderModule vert(get_shader_path("VulkanTests/CanvasToScreen.vert.spv").string().c_str());
        static VulkanShaderModule frag(get_shader_path("VulkanTests/CanvasToScreen.frag.spv").string().c_str());
        static VkPipelineShaderStageCreateInfo shader_stages[2] = {
            vert.stage_create_info(VK_SHADER_STAGE_VERTEX_BIT),
            frag.stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT)
        };
        GraphicsPipelineCreateInfoPack pack;
        pack.create_info.layout = pipeline_layout_;
        pack.create_info.renderPass = get_shared_render_pass();
        pack.input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        pack.viewports.emplace_back(0.f, 0.f, float(window_size.width), float(window_size.height), 0.f, 1.f);
        pack.scissors.emplace_back(VkOffset2D{}, window_size);
        pack.multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        pack.rasterization_state_create_info.lineWidth = 1.f;
        pack.color_blend_attachment_states.push_back({ .colorWriteMask = 0b1111 });
        pack.update_all_arrays();
        pack.create_info.stageCount = 2;
        pack.create_info.pStages = shader_stages;
        return pipeline_.create(pack) == VK_SUCCESS;
    }

    bool create_descriptor_resources() {
        VkDescriptorPoolSize pool_sizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
        };
        descriptor_pool_ = std::make_unique<VulkanDescriptorPool>(1, pool_sizes);
        descriptor_set_ = std::make_unique<VulkanDescriptorSet>();
        return descriptor_pool_->allocate_sets(*descriptor_set_, descriptor_set_layout_) == VK_SUCCESS;
    }

    // 描述符里的 image view 来自 FrameGraph 创建的资源，因此每帧 prepare() 之后写入
    void update_canvas_descriptor(framegraph::ResourceHandle canvas) {
        VkDescriptorImageInfo image_info{
            .sampler = *sampler_,
            .imageView = executor_.image_view(canvas),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        descriptor_set_->write(image_info, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    }

    // pass 1：离屏画布（dynamic rendering）
    void record_canvas_pass(framegraph::PassContext& context, framegraph::ResourceHandle canvas) {
        auto* frame = static_cast<framegraph::FrameGraphExecution*>(context.user_data);
        VkCommandBuffer command_buffer = frame->command_buffer;

        VkRenderingAttachmentInfo color_attachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        color_attachment.imageView = frame->image_view(canvas);
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = clear_canvas_ ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue = { .color = { 0.f, 0.f, 0.f, 1.f } };

        VkRenderingInfo rendering_info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        rendering_info.renderArea = { {}, window_size };
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &color_attachment;

        vkCmdBeginRendering(command_buffer, &rendering_info);
        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_line_);
        vkCmdPushConstants(command_buffer, pipeline_layout_line_, VK_SHADER_STAGE_VERTEX_BIT, 0, 24, &push_constants_offscreen_);
        vkCmdDraw(command_buffer, 2, 1, 0, 0);
        vkCmdEndRendering(command_buffer);
    }

    // pass 2：把画布合成到屏幕（读画布为 sampled，barrier 由图规划）
    void record_composite_pass(framegraph::PassContext& context) {
        auto* frame = static_cast<framegraph::FrameGraphExecution*>(context.user_data);
        VkCommandBuffer command_buffer = frame->command_buffer;
        const uint32_t image_index = VulkanSwapchainManager::get_singleton().get_current_image_index();
        const auto& [render_pass, framebuffers] = VulkanPipelineManager::get_singleton().get_rpwf_screen();

        VkClearValue clear_color = { .color = { 1.f, 1.f, 1.f, 1.f } };
        render_pass.cmd_begin(command_buffer, framebuffers[image_index], { {}, window_size }, clear_color);

        vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        const auto& swapchain_info = VulkanSwapchainManager::get_singleton().get_swapchain_create_info();
        const glm::vec2 window_size_f = { static_cast<float>(swapchain_info.imageExtent.width),
                                          static_cast<float>(swapchain_info.imageExtent.height) };
        vkCmdPushConstants(command_buffer, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, &window_size_f);
        push_constants_offscreen_.viewportSize = window_size_f;
        vkCmdPushConstants(command_buffer, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 8, 8,
                           &push_constants_offscreen_.viewportSize);
        vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
                                descriptor_set_->Address(), 0, nullptr);
        vkCmdDraw(command_buffer, 4, 1, 0, 0);

        render_pass.cmd_end(command_buffer);
    }
};