#pragma once

#include "FrameGraphTypes.h"

#include <span>
#include <string>
#include <vector>

namespace framegraph {

// 依据资源访问推导 pass 之间的依赖（RAW / WAR / WAW，外加显式依赖），并做稳定拓扑排序：
// 没有依赖关系的 pass 保持声明顺序。
bool compute_execution_order(const std::vector<PassInfo>& declared_passes,
                             std::span<const ResourceInfo> resources,
                             std::vector<uint32_t>& execution_order,
                             std::string& error);

// 按编译后的执行顺序统计每个资源的 first/last use 与使用次数。
void compute_lifetimes(std::span<const uint32_t> execution_order,
                       const std::vector<PassInfo>& declared_passes,
                       std::span<ResourceInfo> resources);

}  // namespace framegraph