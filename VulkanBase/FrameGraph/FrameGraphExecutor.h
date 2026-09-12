#pragma once

#include "FrameGraph.h"

#include <vulkan/vulkan.h>

#include <memory>
#include <span>
#include <string>

namespace framegraph {

class FrameGraphExecutor;

// 传给 pass 回调的执行上下文（通过 PassContext::user_data 传递）：
//
//   graph.add_graphics_pass("Draw")
//        .write(canvas, usage::color_attachment_write())
//        .execute([](PassContext& context) {
//            auto& frame = *static_cast<FrameGraphExecution*>(context.user_data);
//            VkCommandBuffer command_buffer = frame.command_buffer;
//            VkImageView view = frame.image_view(canvas);
//        });
struct FrameGraphExecution {
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    const FrameGraphExecutor* executor = nullptr;

    [[nodiscard]] VkImage image(ResourceHandle handle) const noexcept;
    [[nodiscard]] VkImageView image_view(ResourceHandle handle) const noexcept;
    [[nodiscard]] VkBuffer buffer(ResourceHandle handle) const noexcept;
};

// 一个"用图资源拼出来的" render target 附件描述：layout 必须是图规划给该资源的 layout
// （COLOR_ATTACHMENT_OPTIMAL / DEPTH_STENCIL_ATTACHMENT_OPTIMAL / SHADER_READ_ONLY_OPTIMAL ...），
// 这样 render pass 内部不会再发生隐式 layout 转换，转换全部来自图的 barrier。
struct RenderTargetAttachment {
    ResourceHandle resource;
    // subpass 内使用的 layout；initial_layout / final_layout 为 MAX_ENUM 时沿用 layout。
    VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_MAX_ENUM;
    VkImageLayout final_layout = VK_IMAGE_LAYOUT_MAX_ENUM;
    VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
    VkAttachmentLoadOp stencil_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkAttachmentStoreOp stencil_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    bool depth_stencil = false;
};

// executor 持有的 render pass + framebuffer（按附件视图缓存，帧间复用）。
struct RenderTarget {
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkExtent2D extent{};
};

// 设备侧执行器：把 FrameGraph 的资源声明落实为真实的 VkImage / VkBuffer，按编译结果
// 录制 barrier（synchronization2 或等价的旧路径 barrier），再依次调用 pass 回调。
//
// - transient 资源按「名字 + 描述」缓存复用，描述变化时重建，图中不再出现的会释放；
// - 导入资源（swapchain image、外部 buffer）由调用方通过 import_* 绑定；
// - 资源销毁依赖 VulkanCore 单例，因此只能在没有 in-flight 工作（例如每帧 wait fence）时重建。
class FrameGraphExecutor {
public:
    struct Stats {
        uint32_t created_textures = 0;
        uint32_t created_buffers = 0;
        uint32_t reused_textures = 0;
        uint32_t reused_buffers = 0;
        uint32_t destroyed_resources = 0;
        uint32_t image_barriers = 0;
        uint32_t buffer_barriers = 0;
        bool synchronization2 = false;
    };

    FrameGraphExecutor();
    ~FrameGraphExecutor();
    FrameGraphExecutor(const FrameGraphExecutor&) = delete;
    FrameGraphExecutor& operator=(const FrameGraphExecutor&) = delete;

    // true 时用 vkCmdPipelineBarrier2 录制；否则把计划的 stage/access 翻译到旧路径。
    void set_synchronization2(bool enabled) noexcept;

    void import_texture(ResourceHandle handle, VkImage image, VkImageView view = VK_NULL_HANDLE);
    void import_buffer(ResourceHandle handle, VkBuffer buffer);

    // 创建/复用 transient 资源并解析导入绑定；失败时 get_error() 给出原因。
    bool prepare(const FrameGraph& graph);
    // 录制每个 pass 的 barrier 并执行 pass 回调；需要先 prepare()。
    void execute(FrameGraph& graph, VkCommandBuffer command_buffer);

    // 释放全部缓存资源与导入绑定（资源被 in-flight 命令使用时不允许调用）。
    void reset();

    [[nodiscard]] VkImage image(ResourceHandle handle) const noexcept;
    [[nodiscard]] VkImageView image_view(ResourceHandle handle) const noexcept;
    [[nodiscard]] VkBuffer buffer(ResourceHandle handle) const noexcept;

    // 用图资源（含导入资源）拼出兼容现有 VkRenderPass 管线的 render pass + framebuffer；
    // 附件 layout 固定为传入的 layout，同步来自图的 barrier。返回对象由 executor 持有。
    [[nodiscard]] const RenderTarget* acquire_render_target(const FrameGraph& graph,
                                                            std::span<const RenderTargetAttachment> attachments);

    [[nodiscard]] const std::string& get_error() const noexcept;
    [[nodiscard]] const Stats& get_stats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace framegraph