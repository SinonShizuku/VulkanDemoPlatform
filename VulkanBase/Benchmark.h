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
        VkQueryPoolCreateInfo create_info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        create_info.queryCount = 2;
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

    void write_begin(VkCommandBuffer command_buffer) const {
        if (!available_)
            return;
        vkCmdResetQueryPool(command_buffer, pool_, 0, 2);
        vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool_, 0);
    }

    void write_end(VkCommandBuffer command_buffer) const {
        if (!available_)
            return;
        vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, 1);
    }

    // 必须在等待该帧 fence 之后调用。
    [[nodiscard]] double resolve_gpu_ms() const {
        if (!available_)
            return -1.0;
        uint64_t timestamps[2] = {};
        const VkResult result = vkGetQueryPoolResults(
            VulkanCore::get_singleton().get_vulkan_device().get_device(), pool_, 0, 2,
            sizeof(timestamps), timestamps, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (result != VK_SUCCESS)
            return -1.0;
        return static_cast<double>(timestamps[1] - timestamps[0]) * timestamp_period_ns_ / 1e6;
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
        summary.close();

        outstream << std::format("[ Benchmark ] frames={} cpu_ms p50={:.3f} p95={:.3f} p99={:.3f} | gpu_ms p50={:.3f} p95={:.3f} p99={:.3f}\n",
                                 cpu_ms_.size(), cpu_p50, cpu_p95, cpu_p99, gpu_p50, gpu_p95, gpu_p99);
        outstream << std::format("[ Benchmark ] wrote {} and {}\n", base_path + "-frames.csv", base_path + "-summary.csv");
        return true;
    }

private:
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
