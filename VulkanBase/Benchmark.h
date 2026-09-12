#pragma once
#include "../Start.h"
#include "VulkanCore.h"

#include <algorithm>
#include <fstream>
#include <vector>

// 极简 benchmark：每帧两个 timestamp（TOP_OF_PIPE / BOTTOM_OF_PIPE）+ CPU 帧时间。
// 用法：主循环在渲染前把 active_frame_benchmark 指向 recorder，VulkanCommandBuffer::begin/end
// 会自动写入两个 timestamp（因此不需要改动任何 demo）；fence 等待之后调用 resolve_gpu_ms()。
class BenchmarkRecorder {
public:
    void initialize(VkPhysicalDevice physical_device) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        timestamp_period_ns_ = properties.limits.timestampPeriod;
        if (timestamp_period_ns_ <= 0.0f) {
            outstream << "[ Benchmark ] 该设备不支持 timestamp query，GPU 时间将不可用\n";
            return;
        }
        // 0 = 帧开始；1..kMaxPassSlots = 各 pass 起点；kFrameEndIndex = 帧结束。
        VkQueryPoolCreateInfo create_info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        create_info.queryCount = kQueryCount;
        if (vkCreateQueryPool(VulkanCore::get_singleton().get_vulkan_device().get_device(), &create_info, nullptr, &pool_) != VK_SUCCESS) {
            pool_ = VK_NULL_HANDLE;
            outstream << "[ Benchmark ] 创建 timestamp query pool 失败\n";
            return;
        }
        available_ = true;
    }
    void shutdown() {
        if (pool_ != VK_NULL_HANDLE) {
            vkDestroyQueryPool(VulkanCore::get_singleton().get_vulkan_device().get_device(), pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
        available_ = false;
    }

    [[nodiscard]] bool available() const { return available_; }

    void write_begin(VkCommandBuffer command_buffer) {
        if (!available_)
            return;
        touched_slots_.clear();
        frame_pass_names_.clear();
        vkCmdResetQueryPool(command_buffer, pool_, 0, kQueryCount);
        vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool_, 0);
    }

    // 由图中每个 pass 的开头调用（FrameGraph executor 统一写，不需要改 demo）。
    void write_pass(VkCommandBuffer command_buffer, const char* pass_name) {
        if (!available_ || pass_name == nullptr)
            return;
        const int slot = slot_for(pass_name);
        if (slot < 0)
            return;
        if (std::find(touched_slots_.begin(), touched_slots_.end(), slot) == touched_slots_.end())
            touched_slots_.push_back(slot);
        vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool_, static_cast<uint32_t>(1 + slot));
    }

    void write_end(VkCommandBuffer command_buffer) {
        if (!available_)
            return;
        // 未使用的 slot 也要写：否则 vkGetQueryPoolResults(WAIT_BIT) 会一直等一个永远不可用的结果。
        for (uint32_t slot = 0; slot < kMaxPassSlots; ++slot) {
            if (std::find(touched_slots_.begin(), touched_slots_.end(), static_cast<int>(slot)) == touched_slots_.end())
                vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, 1 + slot);
        }
        vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, kFrameEndIndex);
    }

    [[nodiscard]] int slot_for(const char* pass_name) {
        for (size_t i = 0; i < pass_names_.size(); ++i)
            if (pass_names_[i] == pass_name)
                return static_cast<int>(i);
        if (pass_names_.size() >= kMaxPassSlots)
            return -1;
        pass_names_.emplace_back(pass_name);
        pass_samples_.emplace_back();
        return static_cast<int>(pass_names_.size() - 1);
    }
    // 必须在等待该帧 fence 之后调用：记录整帧 GPU 时间与每个 pass 的 GPU 时间。
    [[nodiscard]] double resolve_and_record() {
        if (!available_)
            return -1.0;
        std::vector<uint64_t> timestamps(kQueryCount, 0);
        const VkResult result = vkGetQueryPoolResults(
            VulkanCore::get_singleton().get_vulkan_device().get_device(), pool_, 0, kQueryCount,
            timestamps.size() * sizeof(uint64_t), timestamps.data(), sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (result != VK_SUCCESS)
            return -1.0;

        const double to_ms = timestamp_period_ns_ / 1e6;
        const double frame_ms = static_cast<double>(timestamps[kFrameEndIndex] - timestamps[0]) * to_ms;
        for (size_t i = 0; i < touched_slots_.size(); ++i) {
            const uint32_t begin_index = static_cast<uint32_t>(1 + touched_slots_[i]);
            const uint32_t end_index = (i + 1 < touched_slots_.size())
                                           ? static_cast<uint32_t>(1 + touched_slots_[i + 1])
                                           : kFrameEndIndex;
            const double pass_ms = static_cast<double>(timestamps[end_index] - timestamps[begin_index]) * to_ms;
            pass_samples_[touched_slots_[i]].push_back(pass_ms);
            frame_pass_names_.push_back(pass_names_[touched_slots_[i]]);
        }
        return frame_ms;
    }
    void add_sample(double cpu_ms, double gpu_ms) {
        cpu_ms_.push_back(cpu_ms);
        gpu_ms_.push_back(gpu_ms);
    }

    [[nodiscard]] size_t sample_count() const { return cpu_ms_.size(); }

    // 写 frames.csv（逐帧）与 summary.csv，并把分位数打到 stdout。
    bool write_reports(const std::string& base_path, const std::vector<std::string>& metadata) const {
        if (cpu_ms_.empty())
            return false;
        std::ofstream frames(base_path + "-frames.csv");
        if (!frames)
            return false;
        frames << "frame,cpu_ms,gpu_ms\n";
        for (size_t i = 0; i < cpu_ms_.size(); ++i)
            frames << i << "," << cpu_ms_[i] << "," << (i < gpu_ms_.size() ? gpu_ms_[i] : -1.0) << "\n";
        frames.close();

        const double cpu_p50 = percentile(cpu_ms_, 0.50);
        const double cpu_p95 = percentile(cpu_ms_, 0.95);
        const double cpu_p99 = percentile(cpu_ms_, 0.99);
        const double gpu_p50 = percentile(gpu_ms_, 0.50);
        const double gpu_p95 = percentile(gpu_ms_, 0.95);
        const double gpu_p99 = percentile(gpu_ms_, 0.99);

        std::ofstream summary(base_path + "-summary.csv");
        if (!summary)
            return false;
        summary << "key,value\n";
        for (const std::string& line : metadata)
            summary << line << "\n";
        summary << "frames," << cpu_ms_.size() << "\n";
        summary << "cpu_ms_p50," << cpu_p50 << "\n";
        summary << "cpu_ms_p95," << cpu_p95 << "\n";
        summary << "cpu_ms_p99," << cpu_p99 << "\n";
        summary << "gpu_ms_p50," << gpu_p50 << "\n";
        summary << "gpu_ms_p95," << gpu_p95 << "\n";
        summary << "gpu_ms_p99," << gpu_p99 << "\n";
        for (size_t i = 0; i < pass_names_.size(); ++i) {
            if (pass_samples_[i].empty())
                continue;
            summary << "gpu_ms_p50[" << pass_names_[i] << "]," << percentile(pass_samples_[i], 0.50) << "\n";
            summary << "gpu_ms_p95[" << pass_names_[i] << "]," << percentile(pass_samples_[i], 0.95) << "\n";
        }
        summary.close();

        outstream << std::format("[ Benchmark ] frames={} cpu_ms p50={:.3f} p95={:.3f} p99={:.3f} | gpu_ms p50={:.3f} p95={:.3f} p99={:.3f}\n",
                                 cpu_ms_.size(), cpu_p50, cpu_p95, cpu_p99, gpu_p50, gpu_p95, gpu_p99);
        outstream << std::format("[ Benchmark ] wrote {} and {}\n", base_path + "-frames.csv", base_path + "-summary.csv");
        return true;
    }

private:
    static constexpr uint32_t kMaxPassSlots = 20;
    static constexpr uint32_t kFrameEndIndex = 1 + kMaxPassSlots;
    static constexpr uint32_t kQueryCount = kFrameEndIndex + 1;

    static double percentile(const std::vector<double>& samples, double ratio) {
        if (samples.empty())
            return -1.0;
        std::vector<double> sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        const size_t index = std::min(sorted.size() - 1, static_cast<size_t>(ratio * static_cast<double>(sorted.size())));
        return sorted[index];
    }

    VkQueryPool pool_ = VK_NULL_HANDLE;
    bool available_ = false;
    float timestamp_period_ns_ = 0.0f;
    std::vector<double> cpu_ms_;
    std::vector<double> gpu_ms_;
    std::vector<std::string> pass_names_;
    std::vector<std::vector<double>> pass_samples_;
    std::vector<int> touched_slots_;
    std::vector<std::string> frame_pass_names_;
};

// 主循环在渲染前设置；VulkanCommandBuffer::begin/end 会据此写入 timestamp。
inline BenchmarkRecorder* active_frame_benchmark = nullptr;
inline void benchmark_hook_begin(VkCommandBuffer command_buffer) {
    if (active_frame_benchmark)
        active_frame_benchmark->write_begin(command_buffer);
}
inline void benchmark_hook_end(VkCommandBuffer command_buffer) {
    if (active_frame_benchmark)
        active_frame_benchmark->write_end(command_buffer);
}

inline void benchmark_hook_pass(VkCommandBuffer command_buffer, const char* pass_name) {
    if (active_frame_benchmark)
        active_frame_benchmark->write_pass(command_buffer, pass_name);
}
