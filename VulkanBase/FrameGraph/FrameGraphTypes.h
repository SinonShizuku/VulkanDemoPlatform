#pragma once

// FrameGraph 的公共类型。
//
// 这一层只使用 Vulkan 头文件里的枚举与位掩码（格式、layout、synchronization2 的
// stage/access），不创建也不持有任何 VkDevice / VkImage / VkBuffer，因此图编译、
// 依赖分析、生命周期统计与 barrier 规划都能在没有 device 的情况下被单元测试覆盖。

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace framegraph {

inline constexpr uint32_t invalid_index = UINT32_MAX;

// --------------------------------------------------------------------- 句柄

enum class ResourceKind : uint32_t {
    None = 0,
    Texture,
    Buffer,
};

// 资源句柄：只标识图中的资源，底层 Vulkan 资源由后续的 executor 分配并绑定。
struct ResourceHandle {
    uint32_t index = invalid_index;
    ResourceKind kind = ResourceKind::None;

    [[nodiscard]] bool valid() const noexcept {
        return index != invalid_index && kind != ResourceKind::None;
    }
    explicit operator bool() const noexcept { return valid(); }

    friend bool operator==(const ResourceHandle& lhs, const ResourceHandle& rhs) noexcept {
        return lhs.index == rhs.index && lhs.kind == rhs.kind;
    }
};

enum class PassKind : uint32_t {
    Graphics = 0,
    Compute,
    Transfer,
};

constexpr const char* to_string(PassKind kind) noexcept {
    switch (kind) {
        case PassKind::Graphics: return "graphics";
        case PassKind::Compute: return "compute";
        case PassKind::Transfer: return "transfer";
    }
    return "unknown";
}

// --------------------------------------------------------------- 资源描述

enum class ImageUsage : uint32_t {
    None = 0,
    ColorAttachment = 1u << 0,
    DepthStencilAttachment = 1u << 1,
    Sampled = 1u << 2,
    Storage = 1u << 3,
    TransferSrc = 1u << 4,
    TransferDst = 1u << 5,
    Present = 1u << 6,
};

constexpr ImageUsage operator|(ImageUsage lhs, ImageUsage rhs) noexcept {
    return static_cast<ImageUsage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr bool has_usage(ImageUsage set, ImageUsage flag) noexcept {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0u;
}

constexpr const char* to_string(ImageUsage usage) noexcept {
    switch (usage) {
        case ImageUsage::ColorAttachment: return "ColorAttachment";
        case ImageUsage::DepthStencilAttachment: return "DepthStencilAttachment";
        case ImageUsage::Sampled: return "Sampled";
        case ImageUsage::Storage: return "Storage";
        case ImageUsage::TransferSrc: return "TransferSrc";
        case ImageUsage::TransferDst: return "TransferDst";
        case ImageUsage::Present: return "Present";
        case ImageUsage::None: return "None";
    }
    return "None";
}

enum class BufferUsage : uint32_t {
    None = 0,
    Vertex = 1u << 0,
    Index = 1u << 1,
    Uniform = 1u << 2,
    Storage = 1u << 3,
    Indirect = 1u << 4,
    TransferSrc = 1u << 5,
    TransferDst = 1u << 6,
};

constexpr BufferUsage operator|(BufferUsage lhs, BufferUsage rhs) noexcept {
    return static_cast<BufferUsage>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr bool has_usage(BufferUsage set, BufferUsage flag) noexcept {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0u;
}

constexpr const char* to_string(BufferUsage usage) noexcept {
    switch (usage) {
        case BufferUsage::Vertex: return "Vertex";
        case BufferUsage::Index: return "Index";
        case BufferUsage::Uniform: return "Uniform";
        case BufferUsage::Storage: return "Storage";
        case BufferUsage::Indirect: return "Indirect";
        case BufferUsage::TransferSrc: return "TransferSrc";
        case BufferUsage::TransferDst: return "TransferDst";
        case BufferUsage::None: return "None";
    }
    return "None";
}

// 供后续 executor 构造 VkImageCreateInfo / VkBufferCreateInfo 使用。
inline VkImageUsageFlags to_vk_image_usage(ImageUsage usage) noexcept {
    VkImageUsageFlags flags = 0;
    if (has_usage(usage, ImageUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (has_usage(usage, ImageUsage::DepthStencilAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (has_usage(usage, ImageUsage::Sampled)) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (has_usage(usage, ImageUsage::Storage)) flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (has_usage(usage, ImageUsage::TransferSrc)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (has_usage(usage, ImageUsage::TransferDst)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return flags;
}

inline VkBufferUsageFlags to_vk_buffer_usage(BufferUsage usage) noexcept {
    VkBufferUsageFlags flags = 0;
    if (has_usage(usage, BufferUsage::Vertex)) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (has_usage(usage, BufferUsage::Index)) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (has_usage(usage, BufferUsage::Uniform)) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (has_usage(usage, BufferUsage::Storage)) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (has_usage(usage, BufferUsage::Indirect)) flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (has_usage(usage, BufferUsage::TransferSrc)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (has_usage(usage, BufferUsage::TransferDst)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}

// 深度/模板格式需要用对应 aspect，其余按颜色处理。
inline VkImageAspectFlags image_aspect_for(VkFormat format) noexcept {
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

struct TextureDesc {
    std::string name;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{ 1u, 1u, 1u };
    uint32_t mip_levels = 1;
    uint32_t array_layers = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    ImageUsage usage = ImageUsage::None;
};

struct BufferDesc {
    std::string name;
    VkDeviceSize size = 0;
    BufferUsage usage = BufferUsage::None;
};

// ----------------------------------------------------------------- 访问语义

// 一次访问需要的同步语义：执行阶段、访问位、资源需要处于的 layout，以及是否为写操作。
// stage/access 使用 synchronization2 的位，因此可以直接用于 vkCmdPipelineBarrier2；
// 旧路径（vkCmdPipelineBarrier）由 executor 在录制时自行降级转换。
struct Usage {
    ResourceKind kind = ResourceKind::Texture;
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool writes = false;
    // 该访问对资源的用途要求，用于校验 TextureDesc/BufferDesc 是否声明了对应 usage
    ImageUsage image_requirement = ImageUsage::None;
    BufferUsage buffer_requirement = BufferUsage::None;

    [[nodiscard]] Usage with_stages(VkPipelineStageFlags2 new_stages) const noexcept {
        Usage copy = *this;
        copy.stages = new_stages;
        return copy;
    }
};

namespace usage {

inline Usage color_attachment_write() noexcept {
    Usage usage;
    usage.stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    usage.access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    usage.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    usage.writes = true;
    usage.image_requirement = ImageUsage::ColorAttachment;
    return usage;
}

inline Usage color_attachment_read() noexcept {
    Usage usage;
    usage.stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    usage.access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
    usage.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    usage.image_requirement = ImageUsage::ColorAttachment;
    return usage;
}

inline Usage depth_stencil_write() noexcept {
    Usage usage;
    usage.stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    usage.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    usage.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    usage.writes = true;
    usage.image_requirement = ImageUsage::DepthStencilAttachment;
    return usage;
}

inline Usage depth_stencil_read() noexcept {
    Usage usage;
    usage.stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    usage.access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    usage.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    usage.image_requirement = ImageUsage::DepthStencilAttachment;
    return usage;
}

// 着色器读取（采样）：默认发生在片元着色器，需要时用 with_stages() 覆盖。
inline Usage sampled_read() noexcept {
    Usage usage;
    usage.stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    usage.access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    usage.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    usage.image_requirement = ImageUsage::Sampled;
    return usage;
}

inline Usage storage_read() noexcept {
    Usage usage;
    usage.stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    usage.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    usage.layout = VK_IMAGE_LAYOUT_GENERAL;
    usage.image_requirement = ImageUsage::Storage;
    return usage;
}

inline Usage storage_write() noexcept {
    Usage usage;
    usage.stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    usage.access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    usage.layout = VK_IMAGE_LAYOUT_GENERAL;
    usage.writes = true;
    usage.image_requirement = ImageUsage::Storage;
    return usage;
}

inline Usage transfer_src() noexcept {
    Usage usage;
    usage.stages = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    usage.access = VK_ACCESS_2_TRANSFER_READ_BIT;
    usage.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    usage.image_requirement = ImageUsage::TransferSrc;
    return usage;
}

inline Usage transfer_dst() noexcept {
    Usage usage;
    usage.stages = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    usage.access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    usage.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    usage.writes = true;
    usage.image_requirement = ImageUsage::TransferDst;
    return usage;
}

// 交给 presentation engine：本质是一次 layout 转换，因此按写依赖处理。
inline Usage present() noexcept {
    Usage usage;
    usage.stages = VK_PIPELINE_STAGE_2_NONE;
    usage.access = VK_ACCESS_2_NONE;
    usage.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    usage.writes = true;
    usage.image_requirement = ImageUsage::Present;
    return usage;
}

// ---- buffer 访问（buffer 没有 layout） ----

inline Usage vertex_input() noexcept {
    Usage usage;
    usage.kind = ResourceKind::Buffer;
    usage.stages = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    usage.access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    usage.buffer_requirement = BufferUsage::Vertex;
    return usage;
}

inline Usage index_input() noexcept {
    Usage usage;
    usage.kind = ResourceKind::Buffer;
    usage.stages = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
    usage.access = VK_ACCESS_2_INDEX_READ_BIT;
    usage.buffer_requirement = BufferUsage::Index;
    return usage;
}

inline Usage uniform_read() noexcept {
    Usage usage;
    usage.kind = ResourceKind::Buffer;
    usage.stages = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    usage.access = VK_ACCESS_2_UNIFORM_READ_BIT;
    usage.buffer_requirement = BufferUsage::Uniform;
    return usage;
}

inline Usage storage_buffer_read() noexcept {
    Usage usage;
    usage.kind = ResourceKind::Buffer;
    usage.stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    usage.access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    usage.buffer_requirement = BufferUsage::Storage;
    return usage;
}

inline Usage storage_buffer_write() noexcept {
    Usage usage;
    usage.kind = ResourceKind::Buffer;
    usage.stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    usage.access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    usage.writes = true;
    usage.buffer_requirement = BufferUsage::Storage;
    return usage;
}

inline Usage indirect_read() noexcept {
    Usage usage;
    usage.kind = ResourceKind::Buffer;
    usage.stages = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    usage.access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    usage.buffer_requirement = BufferUsage::Indirect;
    return usage;
}

}  // namespace usage

// ------------------------------------------------------------------ 编译结果

struct ResourceAccess {
    ResourceHandle resource;
    Usage usage;
};

// 一个 pass 之前需要提交的 image barrier。
// old_layout == new_layout 表示只做依赖同步、不做 layout 转换。
struct ImageBarrierPlan {
    ResourceHandle resource;
    VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout new_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 src_stages = VK_PIPELINE_STAGE_2_NONE;
    VkPipelineStageFlags2 dst_stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 src_access = VK_ACCESS_2_NONE;
    VkAccessFlags2 dst_access = VK_ACCESS_2_NONE;
    uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED;
    uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED;
    VkImageSubresourceRange range{};
    bool layout_transition = false;
    // 依赖来源：first use / imported initial state / read-after-write / write-after-read / write-after-write
    std::string reason;
};

struct BufferBarrierPlan {
    ResourceHandle resource;
    VkPipelineStageFlags2 src_stages = VK_PIPELINE_STAGE_2_NONE;
    VkPipelineStageFlags2 dst_stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 src_access = VK_ACCESS_2_NONE;
    VkAccessFlags2 dst_access = VK_ACCESS_2_NONE;
    uint32_t src_queue_family = VK_QUEUE_FAMILY_IGNORED;
    uint32_t dst_queue_family = VK_QUEUE_FAMILY_IGNORED;
    std::string reason;
};

struct PassContext;

struct PassInfo {
    std::string name;
    PassKind kind = PassKind::Graphics;
    uint32_t declaration_index = invalid_index;
    uint32_t execution_index = invalid_index;
    // 声明顺序的访问列表与显式依赖
    std::vector<ResourceAccess> accesses;
    std::vector<uint32_t> explicit_dependencies;
    // 编译产物：本 pass 之前需要提交的 barrier
    std::vector<ImageBarrierPlan> image_barriers;
    std::vector<BufferBarrierPlan> buffer_barriers;
    std::function<void(PassContext&)> on_execute;
};

// 执行/录制上下文：把当前 pass 需要先提交的 barrier 交给上层。
// v1 只做无 device 的图执行；后续 executor 会在调用 pass 回调之前用这些描述录制
// vkCmdPipelineBarrier2，并把命令缓冲等对象放进 user_data。
struct PassContext {
    const PassInfo* pass = nullptr;
    std::span<const ImageBarrierPlan> image_barriers;
    std::span<const BufferBarrierPlan> buffer_barriers;
    void* user_data = nullptr;
};

struct ResourceInfo {
    ResourceHandle handle;
    std::string name;
    ResourceKind kind = ResourceKind::None;
    bool imported = false;
    TextureDesc texture;
    BufferDesc buffer;
    // 导入资源进入图之前的同步状态；transient 资源固定为 UNDEFINED / NONE / 0
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 initial_stages = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 initial_access = VK_ACCESS_2_NONE;
    // 生命周期：按编译后的执行顺序记录的首次/末次使用下标
    uint32_t first_use = invalid_index;
    uint32_t last_use = invalid_index;
    uint32_t use_count = 0;
};

struct BarrierStats {
    uint32_t passes = 0;
    uint32_t resources = 0;
    uint32_t imported_resources = 0;
    uint32_t transient_resources = 0;
    uint32_t resource_uses = 0;
    uint32_t image_barriers = 0;
    uint32_t buffer_barriers = 0;
    uint32_t layout_transitions = 0;
    // 未插入 barrier 的访问次数（读后读、同 pass 内合并）
    uint32_t elided_barriers = 0;
};

}  // namespace framegraph
