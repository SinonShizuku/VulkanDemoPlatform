#include "FrameGraph.h"

#include "FrameGraphBarrier.h"
#include "FrameGraphCompiler.h"

#include <format>
#include <sstream>
#include <utility>

namespace framegraph {
namespace {

const char* layout_name(VkImageLayout layout) noexcept {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
        case VK_IMAGE_LAYOUT_GENERAL: return "GENERAL";
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL: return "COLOR_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL: return "DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL: return "DEPTH_STENCIL_READ_ONLY_OPTIMAL";
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: return "SHADER_READ_ONLY_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return "TRANSFER_DST_OPTIMAL";
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return "PRESENT_SRC_KHR";
        default: return "OTHER_LAYOUT";
    }
}


std::string access_list(VkAccessFlags2 access) {
    if (access == VK_ACCESS_2_NONE) {
        return "NONE";
    }
    std::string text;
    const auto append = [&text](const char* name) {
        if (!text.empty()) {
            text += '|';
        }
        text += name;
    };
    if (access & VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT) append("INDIRECT_COMMAND_READ");
    if (access & VK_ACCESS_2_INDEX_READ_BIT) append("INDEX_READ");
    if (access & VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT) append("VERTEX_ATTRIBUTE_READ");
    if (access & VK_ACCESS_2_UNIFORM_READ_BIT) append("UNIFORM_READ");
    if (access & VK_ACCESS_2_SHADER_SAMPLED_READ_BIT) append("SHADER_SAMPLED_READ");
    if (access & VK_ACCESS_2_SHADER_STORAGE_READ_BIT) append("SHADER_STORAGE_READ");
    if (access & VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) append("SHADER_STORAGE_WRITE");
    if (access & VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT) append("COLOR_ATTACHMENT_READ");
    if (access & VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) append("COLOR_ATTACHMENT_WRITE");
    if (access & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT) append("DEPTH_STENCIL_ATTACHMENT_READ");
    if (access & VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) append("DEPTH_STENCIL_ATTACHMENT_WRITE");
    if (access & VK_ACCESS_2_TRANSFER_READ_BIT) append("TRANSFER_READ");
    if (access & VK_ACCESS_2_TRANSFER_WRITE_BIT) append("TRANSFER_WRITE");
    return text.empty() ? std::string("OTHER_ACCESS") : text;
}

std::string stage_list(VkPipelineStageFlags2 stages) {
    if (stages == VK_PIPELINE_STAGE_2_NONE) {
        return "NONE";
    }
    std::string text;
    const auto append = [&text](const char* name) {
        if (!text.empty()) {
            text += '|';
        }
        text += name;
    };
    if (stages & VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT) append("TOP_OF_PIPE");
    if (stages & VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT) append("DRAW_INDIRECT");
    if (stages & VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT) append("VERTEX_INPUT");
    if (stages & VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT) append("VERTEX_SHADER");
    if (stages & VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT) append("EARLY_FRAGMENT_TESTS");
    if (stages & VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT) append("FRAGMENT_SHADER");
    if (stages & VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT) append("LATE_FRAGMENT_TESTS");
    if (stages & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) append("COLOR_ATTACHMENT_OUTPUT");
    if (stages & VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT) append("COMPUTE_SHADER");
    if (stages & VK_PIPELINE_STAGE_2_TRANSFER_BIT) append("TRANSFER");
    if (stages & VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT) append("ALL_GRAPHICS");
    if (stages & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) append("ALL_COMMANDS");
    if (stages & VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT) append("BOTTOM_OF_PIPE");
    return text.empty() ? std::string("OTHER_STAGE") : text;
}

}  // namespace

PassBuilder::PassBuilder(FrameGraph& graph, uint32_t pass_index) noexcept
    : graph_(&graph), pass_index_(pass_index) {}

PassBuilder& PassBuilder::read(ResourceHandle resource, const Usage& usage) {
    graph_->add_usage(pass_index_, resource, usage, false);
    return *this;
}

PassBuilder& PassBuilder::write(ResourceHandle resource, const Usage& usage) {
    graph_->add_usage(pass_index_, resource, usage, true);
    return *this;
}

PassBuilder& PassBuilder::depends_on(const PassBuilder& other) {
    graph_->add_dependency(pass_index_, other.declaration_index());
    return *this;
}

PassBuilder& PassBuilder::execute(std::function<void(PassContext&)> callback) {
    graph_->set_pass_callback(pass_index_, std::move(callback));
    return *this;
}

void FrameGraph::reset() {
    name_.clear();
    resources_.clear();
    declared_passes_.clear();
    compiled_passes_.clear();
    execution_order_.clear();
    stats_ = BarrierStats{};
    error_.clear();
    compiled_ = false;
    declaration_valid_ = true;
    begin_pass_callback_ = nullptr;
}

ResourceHandle FrameGraph::create_texture(const TextureDesc& desc) {
    return register_texture(desc, false, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
}

ResourceHandle FrameGraph::create_buffer(const BufferDesc& desc) {
    return register_buffer(desc, false);
}

ResourceHandle FrameGraph::import_texture(const TextureDesc& desc,
                                          VkImageLayout initial_layout,
                                          VkPipelineStageFlags2 initial_stages,
                                          VkAccessFlags2 initial_access) {
    return register_texture(desc, true, initial_layout, initial_stages, initial_access);
}

ResourceHandle FrameGraph::import_buffer(const BufferDesc& desc) {
    return register_buffer(desc, true);
}

ResourceHandle FrameGraph::register_texture(const TextureDesc& desc,
                                            bool imported,
                                            VkImageLayout initial_layout,
                                            VkPipelineStageFlags2 initial_stages,
                                            VkAccessFlags2 initial_access) {
    if (!declaration_valid_) {
        return {};
    }
    if (desc.format == VK_FORMAT_UNDEFINED) {
        fail("纹理声明缺少 VkFormat");
        return {};
    }
    if (desc.extent.width == 0 || desc.extent.height == 0 || desc.extent.depth == 0) {
        fail("纹理声明的 extent 不合法");
        return {};
    }
    if (desc.mip_levels == 0 || desc.array_layers == 0) {
        fail("纹理声明的 mip / array 层数必须至少为 1");
        return {};
    }
    if (desc.usage == ImageUsage::None) {
        fail("纹理没有声明任何用途（TextureDesc::usage）");
        return {};
    }

    ResourceInfo info;
    info.handle = ResourceHandle{ static_cast<uint32_t>(resources_.size()), ResourceKind::Texture };
    info.kind = ResourceKind::Texture;
    info.imported = imported;
    info.texture = desc;
    info.texture.name = desc.name.empty() ? std::format("texture_{}", info.handle.index) : desc.name;
    info.name = info.texture.name;
    info.initial_layout = initial_layout;
    info.initial_stages = initial_stages;
    info.initial_access = initial_access;

    if (has_resource_named(info.name)) {
        fail(std::format("资源名 '{}' 重复", info.name));
        return {};
    }

    resources_.push_back(std::move(info));
    invalidate_compilation();
    return resources_.back().handle;
}

ResourceHandle FrameGraph::register_buffer(const BufferDesc& desc, bool imported) {
    if (!declaration_valid_) {
        return {};
    }
    if (desc.size == 0) {
        fail("buffer 声明的 size 不能为 0");
        return {};
    }
    if (desc.usage == BufferUsage::None) {
        fail("buffer 没有声明任何用途（BufferDesc::usage）");
        return {};
    }

    ResourceInfo info;
    info.handle = ResourceHandle{ static_cast<uint32_t>(resources_.size()), ResourceKind::Buffer };
    info.kind = ResourceKind::Buffer;
    info.imported = imported;
    info.buffer = desc;
    info.buffer.name = desc.name.empty() ? std::format("buffer_{}", info.handle.index) : desc.name;
    info.name = info.buffer.name;

    if (has_resource_named(info.name)) {
        fail(std::format("资源名 '{}' 重复", info.name));
        return {};
    }

    resources_.push_back(std::move(info));
    invalidate_compilation();
    return resources_.back().handle;
}

PassBuilder FrameGraph::add_graphics_pass(std::string_view name) {
    return add_pass(name, PassKind::Graphics);
}

PassBuilder FrameGraph::add_compute_pass(std::string_view name) {
    return add_pass(name, PassKind::Compute);
}

PassBuilder FrameGraph::add_transfer_pass(std::string_view name) {
    return add_pass(name, PassKind::Transfer);
}

PassBuilder FrameGraph::add_pass(std::string_view name, PassKind kind) {
    if (!declaration_valid_) {
        return PassBuilder(*this, invalid_index);
    }
    if (name.empty()) {
        fail("pass 名称不能为空");
        return PassBuilder(*this, invalid_index);
    }
    if (has_pass_named(name)) {
        fail(std::format("pass 名称 '{}' 重复", std::string(name)));
        return PassBuilder(*this, invalid_index);
    }

    PassInfo info;
    info.name = std::string(name);
    info.kind = kind;
    info.declaration_index = static_cast<uint32_t>(declared_passes_.size());
    declared_passes_.push_back(std::move(info));
    invalidate_compilation();
    return PassBuilder(*this, static_cast<uint32_t>(declared_passes_.size() - 1));
}

void FrameGraph::add_usage(uint32_t pass_index, ResourceHandle resource, const Usage& usage, bool expect_write) {
    if (!declaration_valid_) {
        return;
    }
    if (pass_index >= declared_passes_.size()) {
        fail("无效的 pass 下标");
        return;
    }
    const std::string& pass_name = declared_passes_[pass_index].name;

    if (!resource.valid() || resource.index >= resources_.size() || resources_[resource.index].kind != resource.kind) {
        fail(std::format("pass '{}' 使用了无效的资源句柄", pass_name));
        return;
    }

    const ResourceInfo& info = resources_[resource.index];
    if (usage.kind != info.kind) {
        fail(std::format("pass '{}' 对资源 '{}' 使用了与资源类型不匹配的 usage", pass_name, info.name));
        return;
    }
    if (usage.writes != expect_write) {
        fail(std::format("pass '{}' 调用 {}() 但 usage 是{}操作：write() 只接受写入型 usage，read() 只接受读取型 usage",
                         pass_name, expect_write ? "write" : "read", usage.writes ? "写" : "读"));
        return;
    }
    if (info.kind == ResourceKind::Texture) {
        if (usage.image_requirement != ImageUsage::None && !has_usage(info.texture.usage, usage.image_requirement)) {
            fail(std::format("pass '{}' 使用了纹理 '{}' 未声明的用途 {}（TextureDesc::usage 缺少该项）",
                             pass_name, info.name, to_string(usage.image_requirement)));
            return;
        }
    } else {
        if (usage.buffer_requirement != BufferUsage::None && !has_usage(info.buffer.usage, usage.buffer_requirement)) {
            fail(std::format("pass '{}' 使用了 buffer '{}' 未声明的用途 {}（BufferDesc::usage 缺少该项）",
                             pass_name, info.name, to_string(usage.buffer_requirement)));
            return;
        }
    }

    declared_passes_[pass_index].accesses.push_back(ResourceAccess{ resource, usage });
    invalidate_compilation();
}

void FrameGraph::add_dependency(uint32_t pass_index, uint32_t dependency) {
    if (!declaration_valid_) {
        return;
    }
    if (pass_index >= declared_passes_.size()) {
        fail("无效的 pass 下标");
        return;
    }
    declared_passes_[pass_index].explicit_dependencies.push_back(dependency);
    invalidate_compilation();
}

void FrameGraph::set_pass_callback(uint32_t pass_index, std::function<void(PassContext&)> callback) {
    if (!declaration_valid_) {
        return;
    }
    if (pass_index >= declared_passes_.size()) {
        fail("无效的 pass 下标");
        return;
    }
    declared_passes_[pass_index].on_execute = callback;
    // 编译结果保存的是回调副本，这里同步更新，避免设置回调后必须重新编译
    for (PassInfo& pass : compiled_passes_) {
        if (pass.declaration_index == pass_index) {
            pass.on_execute = callback;
        }
    }
}

bool FrameGraph::compile() {
    compiled_ = false;
    compiled_passes_.clear();
    execution_order_.clear();
    stats_ = BarrierStats{};

    if (!declaration_valid_) {
        return false;  // error_ 中保存的是声明阶段的首个错误
    }
    error_.clear();

    std::vector<uint32_t> execution_order;
    if (!compute_execution_order(declared_passes_, resources_, execution_order, error_)) {
        return false;
    }

    compute_lifetimes(execution_order, declared_passes_, resources_);

    std::vector<PassInfo> compiled;
    if (!plan_barriers(execution_order, declared_passes_, resources_, compiled, error_)) {
        return false;
    }

    execution_order_ = std::move(execution_order);
    compiled_passes_ = std::move(compiled);
    compute_stats();
    compiled_ = true;
    return true;
}

void FrameGraph::execute() {
    if (!compiled_) {
        if (error_.empty()) {
            error_ = "FrameGraph 尚未编译，无法执行";
        }
        return;
    }

    for (const PassInfo& pass : compiled_passes_) {
        PassContext context;
        context.pass = &pass;
        context.image_barriers = pass.image_barriers;
        context.buffer_barriers = pass.buffer_barriers;

        if (begin_pass_callback_) {
            begin_pass_callback_(context);
        }
        if (pass.on_execute) {
            pass.on_execute(context);
        }
    }
}

std::string FrameGraph::dump() const {
    std::ostringstream out;
    out << std::format("FrameGraph '{}': compiled={} passes={} resources={} image_barriers={} buffer_barriers={} layout_transitions={} elided={}\n",
                       name_,
                       compiled_ ? "true" : "false",
                       stats_.passes,
                       stats_.resources,
                       stats_.image_barriers,
                       stats_.buffer_barriers,
                       stats_.layout_transitions,
                       stats_.elided_barriers);

    if (!compiled_) {
        if (!error_.empty()) {
            out << std::format("error: {}\n", error_);
        }
        return out.str();
    }

    out << "execution order:\n";
    for (const PassInfo& pass : compiled_passes_) {
        out << std::format("  [{}] {} ({}) declared={}\n",
                           pass.execution_index, pass.name, to_string(pass.kind), pass.declaration_index);
        for (const ResourceAccess& access : pass.accesses) {
            out << std::format("      {} {} [layout={} access={}]\n",
                               access.usage.writes ? "write" : "read ",
                               resources_[access.resource.index].name,
                               layout_name(access.usage.layout),
                               access_list(access.usage.access));
        }
        for (const ImageBarrierPlan& barrier : pass.image_barriers) {
            out << std::format("      image barrier {}: {} -> {} [{}]\n",
                               resources_[barrier.resource.index].name,
                               layout_name(barrier.old_layout),
                               layout_name(barrier.new_layout),
                               barrier.reason);
            out << std::format("          src stage={} access={}\n", stage_list(barrier.src_stages), access_list(barrier.src_access));
            out << std::format("          dst stage={} access={}\n", stage_list(barrier.dst_stages), access_list(barrier.dst_access));
        }
        for (const BufferBarrierPlan& barrier : pass.buffer_barriers) {
            out << std::format("      buffer barrier {}: [{}]\n",
                               resources_[barrier.resource.index].name,
                               barrier.reason);
            out << std::format("          src stage={} access={}\n", stage_list(barrier.src_stages), access_list(barrier.src_access));
            out << std::format("          dst stage={} access={}\n", stage_list(barrier.dst_stages), access_list(barrier.dst_access));
        }
    }

    out << "resources:\n";
    for (const ResourceInfo& info : resources_) {
        const std::string lifetime = (info.first_use == invalid_index)
            ? std::string("unused")
            : std::format("[{}..{}]", info.first_use, info.last_use);
        out << std::format("  {} {} {} lifetime={} uses={}\n",
                           info.name,
                           info.kind == ResourceKind::Texture ? "texture" : "buffer",
                           info.imported ? "imported" : "transient",
                           lifetime,
                           info.use_count);
    }

    return out.str();
}

void FrameGraph::invalidate_compilation() {
    compiled_ = false;
    compiled_passes_.clear();
    execution_order_.clear();
    stats_ = BarrierStats{};
}

void FrameGraph::fail(std::string message) {
    if (declaration_valid_) {
        declaration_valid_ = false;
        error_ = std::move(message);
    }
}

bool FrameGraph::has_resource_named(std::string_view name) const {
    for (const ResourceInfo& info : resources_) {
        if (info.name == name) {
            return true;
        }
    }
    return false;
}

bool FrameGraph::has_pass_named(std::string_view name) const {
    for (const PassInfo& pass : declared_passes_) {
        if (pass.name == name) {
            return true;
        }
    }
    return false;
}

void FrameGraph::compute_stats() {
    stats_ = BarrierStats{};
    stats_.passes = static_cast<uint32_t>(compiled_passes_.size());
    stats_.resources = static_cast<uint32_t>(resources_.size());

    for (const ResourceInfo& info : resources_) {
        if (info.imported) {
            ++stats_.imported_resources;
        } else {
            ++stats_.transient_resources;
        }
        stats_.resource_uses += info.use_count;
    }

    for (const PassInfo& pass : compiled_passes_) {
        stats_.image_barriers += static_cast<uint32_t>(pass.image_barriers.size());
        stats_.buffer_barriers += static_cast<uint32_t>(pass.buffer_barriers.size());
        for (const ImageBarrierPlan& barrier : pass.image_barriers) {
            if (barrier.layout_transition) {
                ++stats_.layout_transitions;
            }
        }
    }

    const uint32_t barriers = stats_.image_barriers + stats_.buffer_barriers;
    stats_.elided_barriers = stats_.resource_uses > barriers ? stats_.resource_uses - barriers : 0;
}

}  // namespace framegraph