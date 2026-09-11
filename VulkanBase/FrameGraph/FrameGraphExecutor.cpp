#include "FrameGraphExecutor.h"

#include "../../Start.h"
#include "../VulkanCore.h"
#include "../VulkanSwapchainManager.h"
#include "../components/VulkanMemory.h"
#include "../components/VulkanRenderPassWithFramebuffers.h"

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

namespace framegraph {
namespace {


// ---------------------------------------------------------------- 掩码翻译
// synchronization2 的 stage/access 在低位与旧路径完全一致，新增的位在 32 位以上，
// 这里把高位映射成语义最接近的旧位；无法映射时退化为 ALL_COMMANDS / MEMORY_*。

VkPipelineStageFlags to_legacy_stages(VkPipelineStageFlags2 stages, bool is_source) noexcept {
    if (stages == VK_PIPELINE_STAGE_2_NONE) {
        return is_source ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }
    constexpr uint64_t low_mask = 0xFFFFFFFFull;
    VkPipelineStageFlags legacy = static_cast<VkPipelineStageFlags>(stages & low_mask);
    if (stages & (VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT)) {
        legacy |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    }
    if (stages & (VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_RESOLVE_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT)) {
        legacy |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    if (stages & VK_PIPELINE_STAGE_2_CLEAR_BIT) {
        legacy |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    if (legacy == 0) {
        legacy = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    return legacy;
}

VkAccessFlags to_legacy_access(VkAccessFlags2 access) noexcept {
    if (access == VK_ACCESS_2_NONE) {
        return 0;
    }
    constexpr uint64_t low_mask = 0xFFFFFFFFull;
    VkAccessFlags legacy = static_cast<VkAccessFlags>(access & low_mask);
    if (access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) {
        legacy |= VK_ACCESS_SHADER_READ_BIT;
    }
    if (access & VK_ACCESS_2_SHADER_STORAGE_READ_BIT) {
        legacy |= VK_ACCESS_SHADER_READ_BIT;
    }
    if (access & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) {
        legacy |= VK_ACCESS_SHADER_WRITE_BIT;
    }
    constexpr VkAccessFlags2 known = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    if (access & ~(low_mask | known)) {
        legacy |= VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }
    return legacy;
}

VkImageSubresourceRange to_subresource_range(const ResourceInfo& info) noexcept {
    VkImageSubresourceRange range{};
    range.aspectMask = image_aspect_for(info.texture.format);
    range.levelCount = info.texture.mip_levels;
    range.layerCount = info.texture.array_layers;
    return range;
}

}  // namespace

// ------------------------------------------------------------------- Impl

struct FrameGraphExecutor::Impl {
    struct TextureEntry {
        std::string key;
        TextureDesc desc;
        VulkanImageMemory memory;
        VulkanImageView view;
        bool used = true;
        // 跨帧复用时的当前 layout：图的 transient 资源每帧重新声明，如果直接按
        // UNDEFINED 起跳会丢弃上一帧的内容，所以录制 barrier 时用真实 layout 替换。
        VkImageLayout last_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool reused = false;
    };

    struct BufferEntry {
        std::string key;
        BufferDesc desc;
        VulkanBufferMemory memory;
        bool used = true;
    };

    struct RenderTargetEntry {
        std::string key;
        VulkanRenderPass render_pass;
        VulkanFramebuffer framebuffer;
        RenderTarget info;
        bool used = true;
    };

    std::vector<std::unique_ptr<TextureEntry>> textures;
    std::vector<std::unique_ptr<BufferEntry>> buffers;
    std::vector<std::unique_ptr<RenderTargetEntry>> render_targets;

    // 本帧 handle -> 设备资源
    std::vector<TextureEntry*> active_textures;
    std::vector<BufferEntry*> active_buffers;
    std::vector<std::pair<uint32_t, VkImage>> imported_textures;
    std::vector<std::pair<uint32_t, VkImageView>> imported_views;
    std::vector<std::pair<uint32_t, VkBuffer>> imported_buffers;

    bool synchronization2 = false;
    std::string error;
    Stats stats;

    static std::string texture_key(const TextureDesc& desc) {
        return std::format("{}|{}|{}x{}x{}|m{}|l{}|s{}|u{}",
                           desc.name,
                           static_cast<int>(desc.format),
                           desc.extent.width, desc.extent.height, desc.extent.depth,
                           desc.mip_levels, desc.array_layers,
                           static_cast<int>(desc.samples),
                           static_cast<uint32_t>(desc.usage));
    }

    static std::string buffer_key(const BufferDesc& desc) {
        return std::format("{}|{}|{}", desc.name, static_cast<uint64_t>(desc.size), static_cast<uint32_t>(desc.usage));
    }

    [[nodiscard]] VkImage find_imported_image(uint32_t index) const noexcept {
        const auto found = std::find_if(imported_textures.begin(), imported_textures.end(),
            [index](const auto& entry) { return entry.first == index; });
        return found == imported_textures.end() ? VK_NULL_HANDLE : found->second;
    }

    [[nodiscard]] VkImageView find_imported_view(uint32_t index) const noexcept {
        const auto found = std::find_if(imported_views.begin(), imported_views.end(),
            [index](const auto& entry) { return entry.first == index; });
        return found == imported_views.end() ? VK_NULL_HANDLE : found->second;
    }

    [[nodiscard]] VkBuffer find_imported_buffer(uint32_t index) const noexcept {
        const auto found = std::find_if(imported_buffers.begin(), imported_buffers.end(),
            [index](const auto& entry) { return entry.first == index; });
        return found == imported_buffers.end() ? VK_NULL_HANDLE : found->second;
    }

    [[nodiscard]] TextureEntry* find_texture(const ResourceHandle& handle) const noexcept {
        if (handle.index >= active_textures.size()) {
            return nullptr;
        }
        return active_textures[handle.index];
    }

    [[nodiscard]] BufferEntry* find_buffer(const ResourceHandle& handle) const noexcept {
        if (handle.index >= active_buffers.size()) {
            return nullptr;
        }
        return active_buffers[handle.index];
    }
};

// -------------------------------------------------------------- 构造与绑定

FrameGraphExecutor::FrameGraphExecutor() : impl_(std::make_unique<Impl>()) {}

FrameGraphExecutor::~FrameGraphExecutor() = default;

void FrameGraphExecutor::set_synchronization2(bool enabled) noexcept {
    impl_->synchronization2 = enabled;
}

void FrameGraphExecutor::import_texture(ResourceHandle handle, VkImage image, VkImageView view) {
    impl_->imported_textures.emplace_back(handle.index, image);
    impl_->imported_views.emplace_back(handle.index, view);
}

void FrameGraphExecutor::import_buffer(ResourceHandle handle, VkBuffer buffer) {
    impl_->imported_buffers.emplace_back(handle.index, buffer);
}

void FrameGraphExecutor::reset() {
    impl_->textures.clear();
    impl_->buffers.clear();
    impl_->active_textures.clear();
    impl_->active_buffers.clear();
    impl_->imported_textures.clear();
    impl_->imported_views.clear();
    impl_->imported_buffers.clear();
}

// ------------------------------------------------------------------ prepare

bool FrameGraphExecutor::prepare(const FrameGraph& graph) {
    impl_->error.clear();
    impl_->active_textures.assign(graph.get_resources().size(), nullptr);
    impl_->active_buffers.assign(graph.get_resources().size(), nullptr);

    for (const ResourceInfo& info : graph.get_resources()) {
        if (info.kind == ResourceKind::Texture) {
            if (info.imported) {
                const VkImage image = impl_->find_imported_image(info.handle.index);
                if (image == VK_NULL_HANDLE) {
                    impl_->error = std::format("导入纹理 '{}' 没有绑定 VkImage（先调用 import_texture）", info.name);
                    return false;
                }
                continue;  // 导入资源不进缓存，访问时直接查 import 表
            }

            const std::string key = Impl::texture_key(info.texture);
            auto found = std::find_if(impl_->textures.begin(), impl_->textures.end(),
                [&key](const std::unique_ptr<Impl::TextureEntry>& entry) { return entry->key == key; });
            if (found != impl_->textures.end()) {
                (*found)->used = true;
                (*found)->reused = true;
                impl_->active_textures[info.handle.index] = found->get();
                ++impl_->stats.reused_textures;
                continue;
            }

            auto entry = std::make_unique<Impl::TextureEntry>();
            entry->key = key;
            entry->desc = info.texture;

            VkImageCreateInfo create_info{};
            create_info.imageType = VK_IMAGE_TYPE_2D;
            create_info.format = info.texture.format;
            create_info.extent = info.texture.extent;
            create_info.mipLevels = info.texture.mip_levels;
            create_info.arrayLayers = info.texture.array_layers;
            create_info.samples = info.texture.samples;
            create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
            create_info.usage = to_vk_image_usage(info.texture.usage);
            create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            if (entry->memory.create(create_info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                impl_->error = std::format("创建纹理 '{}' 失败（格式 {} / {}x{}）", info.name,
                                           static_cast<int>(info.texture.format),
                                           info.texture.extent.width, info.texture.extent.height);
                return false;
            }

            const VkImageSubresourceRange range = to_subresource_range(info);
            if (entry->view.create(entry->memory.Image(), VK_IMAGE_VIEW_TYPE_2D, info.texture.format, range)) {
                impl_->error = std::format("创建纹理 '{}' 的 image view 失败", info.name);
                return false;
            }

            impl_->active_textures[info.handle.index] = entry.get();
            impl_->textures.push_back(std::move(entry));
            ++impl_->stats.created_textures;
            continue;
        }

        if (info.imported) {
            if (impl_->find_imported_buffer(info.handle.index) == VK_NULL_HANDLE) {
                impl_->error = std::format("导入 buffer '{}' 没有绑定 VkBuffer（先调用 import_buffer）", info.name);
                return false;
            }
            continue;
        }

        const std::string key = Impl::buffer_key(info.buffer);
        auto found = std::find_if(impl_->buffers.begin(), impl_->buffers.end(),
            [&key](const std::unique_ptr<Impl::BufferEntry>& entry) { return entry->key == key; });
        if (found != impl_->buffers.end()) {
            (*found)->used = true;
            impl_->active_buffers[info.handle.index] = found->get();
            ++impl_->stats.reused_buffers;
            continue;
        }

        auto entry = std::make_unique<Impl::BufferEntry>();
        entry->key = key;
        entry->desc = info.buffer;

        VkBufferCreateInfo create_info{};
        create_info.size = info.buffer.size;
        create_info.usage = to_vk_buffer_usage(info.buffer.usage);
        create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (entry->memory.create(create_info, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            impl_->error = std::format("创建 buffer '{}' 失败（{} 字节）", info.name, info.buffer.size);
            return false;
        }

        impl_->active_buffers[info.handle.index] = entry.get();
        impl_->buffers.push_back(std::move(entry));
        ++impl_->stats.created_buffers;
    }

    // 释放本帧没有出现的缓存资源（此时上一帧的 GPU 工作已经结束）
    for (auto it = impl_->textures.begin(); it != impl_->textures.end();) {
        if ((*it)->used) {
            (*it)->used = false;
            ++it;
            continue;
        }
        ++impl_->stats.destroyed_resources;
        it = impl_->textures.erase(it);
    }
    for (auto it = impl_->buffers.begin(); it != impl_->buffers.end();) {
        if ((*it)->used) {
            (*it)->used = false;
            ++it;
            continue;
        }
        ++impl_->stats.destroyed_resources;
        it = impl_->buffers.erase(it);
    }

    for (auto it = impl_->render_targets.begin(); it != impl_->render_targets.end();) {
        if ((*it)->used) {
            (*it)->used = false;
            ++it;
            continue;
        }
        ++impl_->stats.destroyed_resources;
        it = impl_->render_targets.erase(it);
    }

    return true;
}

// ------------------------------------------------------------- render target

const RenderTarget* FrameGraphExecutor::acquire_render_target(const FrameGraph& graph,
                                                               std::span<const RenderTargetAttachment> attachments) {
    std::vector<VkImageView> views;
    VkExtent2D extent{};
    int32_t depth_index = -1;
    for (uint32_t index = 0; index < attachments.size(); ++index) {
        const RenderTargetAttachment& attachment = attachments[index];
        const VkImageView view = image_view(attachment.resource);
        if (view == VK_NULL_HANDLE) {
            impl_->error = std::format("render target 附件 {} 没有可用的 VkImageView", index);
            return nullptr;
        }
        views.push_back(view);
        if (attachment.depth_stencil) {
            depth_index = static_cast<int32_t>(index);
        }

        const auto& resources = graph.get_resources();
        if (attachment.resource.index < resources.size()) {
            const TextureDesc& desc = resources[attachment.resource.index].texture;
            extent = VkExtent2D{ desc.extent.width, desc.extent.height };
        }
    }

    // key 里带上视图句柄与附件参数：换 swapchain image / resize / 改 load-op 都会自然生成新条目，
    // 旧条目在下一次 prepare() 里被回收。
    std::string key;
    for (uint32_t index = 0; index < attachments.size(); ++index) {
        key += std::format("{}|{}|{}|{}|{}|{};",
                           reinterpret_cast<uint64_t>(views[index]),
                           static_cast<int>(attachments[index].layout),
                           static_cast<int>(attachments[index].load_op),
                           static_cast<int>(attachments[index].store_op),
                           attachments[index].depth_stencil ? 1 : 0,
                           index);
    }
    key += std::format("{}x{}", extent.width, extent.height);

    const auto found = std::find_if(impl_->render_targets.begin(), impl_->render_targets.end(),
        [&key](const std::unique_ptr<Impl::RenderTargetEntry>& entry) { return entry->key == key; });
    if (found != impl_->render_targets.end()) {
        (*found)->used = true;
        return &(*found)->info;
    }

    auto entry = std::make_unique<Impl::RenderTargetEntry>();
    entry->key = key;

    std::vector<VkAttachmentDescription> descriptions;
    std::vector<VkAttachmentReference> references;
    VkAttachmentReference depth_reference{};
    descriptions.reserve(attachments.size());
    references.reserve(attachments.size());
    for (uint32_t index = 0; index < attachments.size(); ++index) {
        const auto& resource = graph.get_resources()[attachments[index].resource.index];
        VkAttachmentDescription description{};
        description.format = attachments[index].depth_stencil ? resource.texture.format : resource.texture.format;
        description.samples = resource.texture.samples;
        description.loadOp = attachments[index].load_op;
        description.storeOp = attachments[index].store_op;
        description.stencilLoadOp = attachments[index].stencil_load_op;
        description.stencilStoreOp = attachments[index].stencil_store_op;
        // 固定 layout：pass 内部不做转换，转换由图负责
        description.initialLayout = attachments[index].layout;
        description.finalLayout = attachments[index].layout;
        descriptions.push_back(description);

        if (attachments[index].depth_stencil) {
            depth_reference = VkAttachmentReference{ index, attachments[index].layout };
        } else {
            references.push_back(VkAttachmentReference{ index, attachments[index].layout });
        }
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(references.size());
    subpass.pColorAttachments = references.empty() ? nullptr : references.data();
    subpass.pDepthStencilAttachment = depth_index >= 0 ? &depth_reference : nullptr;

    // 现有 demo 的管线是对着 RHI 的 render pass 创建的；render pass 兼容性要求
    // subpass 依赖数量与 src/dst subpass 一致，因此这里复刻 RHI 的那条外部依赖。
    // 真正的同步仍由图生成的 barrier 提供，这条依赖只是兼容性所需。
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = static_cast<uint32_t>(descriptions.size());
    render_pass_info.pAttachments = descriptions.data();
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 1;
    render_pass_info.pDependencies = &dependency;
    if (entry->render_pass.create(render_pass_info) != VK_SUCCESS) {
        impl_->error = std::format("创建 render target 的 render pass 失败（附件数 {}）", descriptions.size());
        return nullptr;
    }

    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = entry->render_pass;
    framebuffer_info.attachmentCount = static_cast<uint32_t>(views.size());
    framebuffer_info.pAttachments = views.data();
    framebuffer_info.width = extent.width;
    framebuffer_info.height = extent.height;
    framebuffer_info.layers = 1;
    if (entry->framebuffer.create(framebuffer_info) != VK_SUCCESS) {
        impl_->error = "创建 render target 的 framebuffer 失败";
        return nullptr;
    }

    entry->info.render_pass = entry->render_pass;
    entry->info.framebuffer = entry->framebuffer;
    entry->info.extent = extent;
    const RenderTarget* result = &entry->info;
    impl_->render_targets.push_back(std::move(entry));
    return result;
}

// ------------------------------------------------------------- 资源访问器

VkImage FrameGraphExecutor::image(ResourceHandle handle) const noexcept {
    if (Impl::TextureEntry* entry = impl_->find_texture(handle)) {
        return entry->memory.Image();
    }
    return impl_->find_imported_image(handle.index);
}

VkImageView FrameGraphExecutor::image_view(ResourceHandle handle) const noexcept {
    if (Impl::TextureEntry* entry = impl_->find_texture(handle)) {
        return entry->view;
    }
    return impl_->find_imported_view(handle.index);
}

VkBuffer FrameGraphExecutor::buffer(ResourceHandle handle) const noexcept {
    if (Impl::BufferEntry* entry = impl_->find_buffer(handle)) {
        return entry->memory.Buffer();
    }
    return impl_->find_imported_buffer(handle.index);
}

VkImage FrameGraphExecution::image(ResourceHandle handle) const noexcept {
    return executor ? executor->image(handle) : VK_NULL_HANDLE;
}

VkImageView FrameGraphExecution::image_view(ResourceHandle handle) const noexcept {
    return executor ? executor->image_view(handle) : VK_NULL_HANDLE;
}

VkBuffer FrameGraphExecution::buffer(ResourceHandle handle) const noexcept {
    return executor ? executor->buffer(handle) : VK_NULL_HANDLE;
}

// ------------------------------------------------------------------ execute

void FrameGraphExecutor::execute(FrameGraph& graph, VkCommandBuffer command_buffer) {
    impl_->stats.image_barriers = 0;
    impl_->stats.buffer_barriers = 0;
    impl_->stats.synchronization2 = impl_->synchronization2;
    FrameGraphExecution execution;
    execution.command_buffer = command_buffer;
    execution.executor = this;

    graph.set_begin_pass_callback([this, command_buffer](PassContext& context) {
        const PassInfo& pass = *context.pass;

        for (const ImageBarrierPlan& plan : pass.image_barriers) {
            const VkImage image_handle = image(plan.resource);
            if (image_handle == VK_NULL_HANDLE) {
                continue;
            }

            // transient 资源跨帧复用：把图规划的 UNDEFINED 起跳换成上一帧的真实 layout，
            // 这样画布这类需要保留内容的资源不会被每帧丢弃。
            Impl::TextureEntry* const texture_entry = impl_->find_texture(plan.resource);
            const VkImageLayout source_layout =
                (texture_entry && texture_entry->reused && plan.old_layout == VK_IMAGE_LAYOUT_UNDEFINED)
                    ? texture_entry->last_layout
                    : plan.old_layout;

            if (impl_->synchronization2) {
                VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                barrier.srcStageMask = plan.src_stages;
                barrier.srcAccessMask = plan.src_access;
                barrier.dstStageMask = plan.dst_stages;
                barrier.dstAccessMask = plan.dst_access;
                barrier.oldLayout = source_layout;
                barrier.newLayout = plan.new_layout;
                barrier.srcQueueFamilyIndex = plan.src_queue_family;
                barrier.dstQueueFamilyIndex = plan.dst_queue_family;
                barrier.image = image_handle;
                barrier.subresourceRange = plan.range;
                VkDependencyInfo dependency_info{};
                dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dependency_info.imageMemoryBarrierCount = 1;
                dependency_info.pImageMemoryBarriers = &barrier;
                vkCmdPipelineBarrier2(command_buffer, &dependency_info);
            } else {
                VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                barrier.srcAccessMask = to_legacy_access(plan.src_access);
                barrier.dstAccessMask = to_legacy_access(plan.dst_access);
                barrier.oldLayout = source_layout;
                barrier.newLayout = plan.new_layout;
                barrier.srcQueueFamilyIndex = plan.src_queue_family;
                barrier.dstQueueFamilyIndex = plan.dst_queue_family;
                barrier.image = image_handle;
                barrier.subresourceRange = plan.range;
                vkCmdPipelineBarrier(command_buffer,
                                     to_legacy_stages(plan.src_stages, true),
                                     to_legacy_stages(plan.dst_stages, false),
                                     0, 0, nullptr, 0, nullptr, 1, &barrier);
            }
            ++impl_->stats.image_barriers;
        }

        for (const BufferBarrierPlan& plan : pass.buffer_barriers) {
            const VkBuffer buffer_handle = buffer(plan.resource);
            if (buffer_handle == VK_NULL_HANDLE) {
                continue;
            }
            const VkDeviceSize size = VK_WHOLE_SIZE;

            if (impl_->synchronization2) {
                VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
                barrier.srcStageMask = plan.src_stages;
                barrier.srcAccessMask = plan.src_access;
                barrier.dstStageMask = plan.dst_stages;
                barrier.dstAccessMask = plan.dst_access;
                barrier.srcQueueFamilyIndex = plan.src_queue_family;
                barrier.dstQueueFamilyIndex = plan.dst_queue_family;
                barrier.buffer = buffer_handle;
                barrier.offset = 0;
                barrier.size = size;
                VkDependencyInfo dependency_info{};
                dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dependency_info.bufferMemoryBarrierCount = 1;
                dependency_info.pBufferMemoryBarriers = &barrier;
                vkCmdPipelineBarrier2(command_buffer, &dependency_info);
            } else {
                VkBufferMemoryBarrier barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
                barrier.srcAccessMask = to_legacy_access(plan.src_access);
                barrier.dstAccessMask = to_legacy_access(plan.dst_access);
                barrier.srcQueueFamilyIndex = plan.src_queue_family;
                barrier.dstQueueFamilyIndex = plan.dst_queue_family;
                barrier.buffer = buffer_handle;
                barrier.offset = 0;
                barrier.size = size;
                vkCmdPipelineBarrier(command_buffer,
                                     to_legacy_stages(plan.src_stages, true),
                                     to_legacy_stages(plan.dst_stages, false),
                                     0, 0, nullptr, 1, &barrier, 0, nullptr);
            }
            ++impl_->stats.buffer_barriers;
        }
    });

    graph.execute(&execution);

    // 记录本帧结束时各 transient 纹理的 layout，供下一帧复用（保留内容）
    for (const PassInfo& pass : graph.get_passes()) {
        for (const ResourceAccess& access : pass.accesses) {
            if (Impl::TextureEntry* entry = impl_->find_texture(access.resource)) {
                entry->last_layout = access.usage.layout;
            }
        }
    }
}

const std::string& FrameGraphExecutor::get_error() const noexcept {
    return impl_->error;
}

const FrameGraphExecutor::Stats& FrameGraphExecutor::get_stats() const noexcept {
    return impl_->stats;
}

}  // namespace framegraph