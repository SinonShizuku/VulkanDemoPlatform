#pragma once

#include "FrameGraphTypes.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace framegraph {

class FrameGraph;

// Pass 声明辅助对象：由 add_*_pass() 返回，用于声明资源访问、显式依赖与执行回调。
// 它只保存图指针与 pass 下标，因此可以在编译之前继续使用（例如稍后补一条 depends_on）。
class PassBuilder {
public:
    PassBuilder(FrameGraph& graph, uint32_t pass_index) noexcept;

    PassBuilder& read(ResourceHandle resource, const Usage& usage);
    PassBuilder& write(ResourceHandle resource, const Usage& usage);
    // 显式顺序依赖：本 pass 必须晚于 other 执行
    PassBuilder& depends_on(const PassBuilder& other);
    PassBuilder& execute(std::function<void(PassContext&)> callback);

    [[nodiscard]] uint32_t declaration_index() const noexcept { return pass_index_; }

private:
    FrameGraph* graph_ = nullptr;
    uint32_t pass_index_ = invalid_index;
};

// FrameGraph v1：资源/pass 声明、依赖分析、生命周期统计与 barrier 规划。
// 该实现不接触任何 Vulkan 对象，实际资源分配与 barrier 录制由后续的 executor 完成。
class FrameGraph {
public:
    using BeginPassCallback = std::function<void(PassContext&)>;

    FrameGraph() = default;
    ~FrameGraph() = default;
    FrameGraph(const FrameGraph&) = delete;
    FrameGraph& operator=(const FrameGraph&) = delete;

    // 每帧重建：清空资源、pass、编译结果与错误
    void reset();

    void set_name(std::string name) { name_ = std::move(name); }
    [[nodiscard]] const std::string& get_name() const noexcept { return name_; }

    // ---------------------------------------------------------- 资源声明

    ResourceHandle create_texture(const TextureDesc& desc);
    ResourceHandle create_buffer(const BufferDesc& desc);

    // 导入外部资源：initial_* 描述资源进入图之前的同步状态。
    // initial_stages 非零时，首次使用之前一定会插入一次依赖 barrier。
    ResourceHandle import_texture(const TextureDesc& desc,
                                  VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED,
                                  VkPipelineStageFlags2 initial_stages = VK_PIPELINE_STAGE_2_NONE,
                                  VkAccessFlags2 initial_access = VK_ACCESS_2_NONE);
    ResourceHandle import_buffer(const BufferDesc& desc);

    // ------------------------------------------------------------ pass 声明

    PassBuilder add_graphics_pass(std::string_view name);
    PassBuilder add_compute_pass(std::string_view name);
    PassBuilder add_transfer_pass(std::string_view name);

    // --------------------------------------------------------------- 编译

    bool compile();
    [[nodiscard]] bool is_compiled() const noexcept { return compiled_; }
    [[nodiscard]] const std::string& get_error() const noexcept { return error_; }
    // 编译成功后按执行顺序返回；编译前/失败后为空
    [[nodiscard]] const std::vector<PassInfo>& get_passes() const noexcept { return compiled_passes_; }
    [[nodiscard]] const std::vector<ResourceInfo>& get_resources() const noexcept { return resources_; }
    [[nodiscard]] const std::vector<uint32_t>& get_execution_order() const noexcept { return execution_order_; }
    [[nodiscard]] const BarrierStats& get_stats() const noexcept { return stats_; }
    [[nodiscard]] std::string dump() const;

    // --------------------------------------------------------------- 执行

    // v1：按编译顺序调用每个 pass 的回调，并把该 pass 之前应提交的 barrier 交给回调。
    // begin_pass_callback 供上层（后续 executor）统一录制 barrier。
    void set_begin_pass_callback(BeginPassCallback callback) { begin_pass_callback_ = std::move(callback); }
    void execute(void* user_data = nullptr);

    // ------------------------------------------------- PassBuilder 内部接口

    void add_usage(uint32_t pass_index, ResourceHandle resource, const Usage& usage, bool expect_write);
    void add_dependency(uint32_t pass_index, uint32_t dependency);
    void set_pass_callback(uint32_t pass_index, std::function<void(PassContext&)> callback);

private:
    ResourceHandle register_texture(const TextureDesc& desc,
                                    bool imported,
                                    VkImageLayout initial_layout,
                                    VkPipelineStageFlags2 initial_stages,
                                    VkAccessFlags2 initial_access);
    ResourceHandle register_buffer(const BufferDesc& desc, bool imported);
    PassBuilder add_pass(std::string_view name, PassKind kind);
    [[nodiscard]] bool has_resource_named(std::string_view name) const;
    [[nodiscard]] bool has_pass_named(std::string_view name) const;
    void invalidate_compilation();
    void fail(std::string message);
    void compute_stats();

    std::string name_;
    std::vector<ResourceInfo> resources_;
    std::vector<PassInfo> declared_passes_;
    std::vector<PassInfo> compiled_passes_;
    std::vector<uint32_t> execution_order_;
    BarrierStats stats_;
    std::string error_;
    bool compiled_ = false;
    bool declaration_valid_ = true;
    BeginPassCallback begin_pass_callback_;
};

}  // namespace framegraph