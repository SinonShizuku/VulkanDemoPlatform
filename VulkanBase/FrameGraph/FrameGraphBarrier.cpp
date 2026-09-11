#include "FrameGraphBarrier.h"

#include <algorithm>
#include <format>
#include <utility>

namespace framegraph {
namespace {

// 单个资源当前已知的同步状态
struct ResourceState {
    bool used = false;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    bool last_write = false;
};

// 同一 pass 内对同一资源的多次访问合并后的结果
struct PendingUsage {
    ResourceHandle resource;
    bool write = false;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

const char* hazard_reason(bool previous_write, bool current_write) noexcept {
    if (previous_write && current_write) {
        return "write-after-write";
    }
    return previous_write ? "read-after-write" : "write-after-read";
}

VkImageAspectFlags aspect_mask_for(VkFormat format) noexcept {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

VkImageSubresourceRange subresource_range_for(const ResourceInfo& info) noexcept {
    VkImageSubresourceRange range{};
    range.aspectMask = aspect_mask_for(info.texture.format);
    range.baseMipLevel = 0;
    range.levelCount = info.texture.mip_levels;
    range.baseArrayLayer = 0;
    range.layerCount = info.texture.array_layers;
    return range;
}

}  // namespace

bool plan_barriers(std::span<const uint32_t> execution_order,
                   const std::vector<PassInfo>& declared_passes,
                   std::span<const ResourceInfo> resources,
                   std::vector<PassInfo>& out_passes,
                   std::string& error) {
    out_passes.clear();
    out_passes.reserve(execution_order.size());

    std::vector<ResourceState> states(resources.size());

    for (uint32_t execution = 0; execution < execution_order.size(); ++execution) {
        if (execution_order[execution] >= declared_passes.size()) {
            error = std::format("执行顺序引用了不存在的 pass 下标 {}", execution_order[execution]);
            return false;
        }

        PassInfo pass = declared_passes[execution_order[execution]];
        pass.execution_index = execution;
        pass.image_barriers.clear();
        pass.buffer_barriers.clear();

        // 1. 合并同一 pass 内对同一资源的多次访问：barrier 在 pass 之前统一提交。
        //    同一 pass 内要求 layout 一致，否则无法用一次转换覆盖（v1 限制）。
        std::vector<PendingUsage> pending;
        pending.reserve(pass.accesses.size());
        for (const ResourceAccess& access : pass.accesses) {
            if (access.resource.index >= resources.size()) {
                error = std::format("pass '{}' 引用了不存在的资源句柄", pass.name);
                return false;
            }
            const auto found = std::find_if(pending.begin(), pending.end(),
                [&access](const PendingUsage& usage) { return usage.resource == access.resource; });
            if (found == pending.end()) {
                pending.push_back(PendingUsage{
                    access.resource,
                    access.usage.writes,
                    access.usage.stages,
                    access.usage.access,
                    access.usage.layout,
                });
                continue;
            }
            if (found->layout != access.usage.layout) {
                error = std::format(
                    "pass '{}' 在同一 pass 内以不同 layout 使用资源 '{}'，v1 不支持 pass 内 layout 变化",
                    pass.name, resources[access.resource.index].name);
                return false;
            }
            found->write = found->write || access.usage.writes;
            found->stages |= access.usage.stages;
            found->access |= access.usage.access;
        }

        // 2. 逐资源生成 barrier
        for (const PendingUsage& use : pending) {
            const ResourceInfo& info = resources[use.resource.index];
            ResourceState& state = states[use.resource.index];

            if (info.kind == ResourceKind::Buffer) {
                if (!state.used) {
                    // buffer 没有 layout，首次使用不需要 barrier
                    state.used = true;
                    state.stages = use.stages;
                    state.access = use.access;
                    state.last_write = use.write;
                    continue;
                }
                if (!state.last_write && !use.write) {
                    state.stages |= use.stages;
                    state.access |= use.access;
                    continue;
                }
                BufferBarrierPlan barrier;
                barrier.resource = use.resource;
                barrier.src_stages = state.stages;
                barrier.src_access = state.last_write ? state.access : VK_ACCESS_2_NONE;
                barrier.dst_stages = use.stages;
                barrier.dst_access = use.access;
                barrier.reason = hazard_reason(state.last_write, use.write);
                pass.buffer_barriers.push_back(std::move(barrier));
                state.stages = use.stages;
                state.access = use.access;
                state.last_write = use.write;
                continue;
            }

            if (!state.used) {
                // transient 资源必须先被写入，否则读到的是未定义内容
                if (!info.imported && !use.write && use.layout != VK_IMAGE_LAYOUT_UNDEFINED) {
                    error = std::format("pass '{}' 在 transient 纹理 '{}' 被写入之前读取它", pass.name, info.name);
                    return false;
                }

                const bool layout_change = info.initial_layout != use.layout;
                const bool pending_external_work = info.initial_stages != VK_PIPELINE_STAGE_2_NONE;
                if (layout_change || pending_external_work) {
                    ImageBarrierPlan barrier;
                    barrier.resource = use.resource;
                    barrier.old_layout = info.initial_layout;
                    barrier.new_layout = use.layout;
                    barrier.src_stages = info.initial_stages;
                    barrier.src_access = info.initial_access;
                    barrier.dst_stages = use.stages;
                    barrier.dst_access = use.access;
                    barrier.range = subresource_range_for(info);
                    barrier.layout_transition = layout_change;
                    barrier.reason = info.imported ? "imported initial state" : "first use";
                    pass.image_barriers.push_back(std::move(barrier));
                }

                state.used = true;
                state.layout = use.layout;
                state.stages = use.stages;
                state.access = use.access;
                state.last_write = use.write;
                continue;
            }

            const bool layout_change = state.layout != use.layout;
            const bool hazard = state.last_write || use.write;
            if (!layout_change && !hazard) {
                // 读后读且 layout 不变：不需要 barrier
                state.stages |= use.stages;
                state.access |= use.access;
                continue;
            }

            ImageBarrierPlan barrier;
            barrier.resource = use.resource;
            barrier.old_layout = state.layout;
            barrier.new_layout = use.layout;
            barrier.src_stages = state.stages;
            // WAR 只需要执行依赖；RAW / WAW 需要让前一次写的结果可见
            barrier.src_access = state.last_write ? state.access : VK_ACCESS_2_NONE;
            barrier.dst_stages = use.stages;
            barrier.dst_access = use.access;
            barrier.range = subresource_range_for(info);
            barrier.layout_transition = layout_change;
            barrier.reason = hazard ? hazard_reason(state.last_write, use.write) : "layout transition";
            pass.image_barriers.push_back(std::move(barrier));

            state.layout = use.layout;
            state.stages = use.stages;
            state.access = use.access;
            state.last_write = use.write;
        }

        out_passes.push_back(std::move(pass));
    }

    return true;
}

}  // namespace framegraph