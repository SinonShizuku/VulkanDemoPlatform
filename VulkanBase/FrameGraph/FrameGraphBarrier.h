#pragma once

#include "FrameGraphTypes.h"

#include <span>
#include <string>
#include <vector>

namespace framegraph {

// 按执行顺序为每个 pass 规划 barrier（v1 规则）：
//   1. 跟踪每个资源最近一次访问的 layout / stage / access 与是否为写；
//   2. 在 pass 之前把资源转换到本次访问需要的 layout；
//   3. 只有 RAW / WAR / WAW 或 layout 变化才插入 barrier，读后读直接省略；
//   4. 合并同一 pass 内对同一资源的多次访问（要求这些访问的 layout 一致）；
//   5. 导入资源使用调用方声明的初始状态，transient 资源以 UNDEFINED 起步；
//   6. WAR 只产生执行依赖（srcAccess == 0），不额外做 cache 失效。
bool plan_barriers(std::span<const uint32_t> execution_order,
                   const std::vector<PassInfo>& declared_passes,
                   std::span<const ResourceInfo> resources,
                   std::vector<PassInfo>& out_passes,
                   std::string& error);

}  // namespace framegraph