#include "FrameGraphCompiler.h"

#include <algorithm>
#include <format>
#include <functional>

namespace framegraph {
namespace {

struct AccessRef {
    uint32_t pass = invalid_index;
    const Usage* usage = nullptr;
};

// 两次访问之间至少有一次写，就必须保持先后顺序（RAW / WAR / WAW）
bool is_hazard(const Usage& previous, const Usage& current) noexcept {
    return previous.writes || current.writes;
}

}  // namespace

bool compute_execution_order(const std::vector<PassInfo>& declared_passes,
                             std::span<const ResourceInfo> resources,
                             std::vector<uint32_t>& execution_order,
                             std::string& error) {
    const uint32_t pass_count = static_cast<uint32_t>(declared_passes.size());
    execution_order.clear();
    if (pass_count == 0) {
        return true;
    }

    std::vector<std::vector<uint32_t>> successors(pass_count);
    std::vector<uint32_t> indegree(pass_count, 0);

    const auto add_edge = [&](uint32_t from, uint32_t to) {
        if (from == to || from >= pass_count || to >= pass_count) {
            return;
        }
        std::vector<uint32_t>& list = successors[from];
        if (std::find(list.begin(), list.end(), to) != list.end()) {
            return;
        }
        list.push_back(to);
        ++indegree[to];
    };

    // 收集每个资源的访问序列：按 pass 声明顺序排列，因此派生出的依赖天然向前
    std::vector<std::vector<AccessRef>> accesses_by_resource(resources.size());
    for (uint32_t pass = 0; pass < pass_count; ++pass) {
        for (const ResourceAccess& access : declared_passes[pass].accesses) {
            if (access.resource.index >= resources.size() ||
                resources[access.resource.index].handle.kind != access.resource.kind) {
                error = std::format("pass '{}' 引用了不存在的资源句柄", declared_passes[pass].name);
                return false;
            }
            accesses_by_resource[access.resource.index].push_back(AccessRef{ pass, &access.usage });
        }
    }

    for (const std::vector<AccessRef>& accesses : accesses_by_resource) {
        for (size_t index = 1; index < accesses.size(); ++index) {
            const AccessRef& previous = accesses[index - 1];
            const AccessRef& current = accesses[index];
            if (previous.pass == current.pass) {
                continue;  // 同一 pass 内的先后由该 pass 自己保证
            }
            if (is_hazard(*previous.usage, *current.usage)) {
                add_edge(previous.pass, current.pass);
            }
        }
    }

    // 显式依赖：dependency 必须先于当前 pass 执行
    for (uint32_t pass = 0; pass < pass_count; ++pass) {
        for (const uint32_t dependency : declared_passes[pass].explicit_dependencies) {
            if (dependency >= pass_count) {
                error = std::format("pass '{}' 的显式依赖下标 {} 越界", declared_passes[pass].name, dependency);
                return false;
            }
            if (dependency == pass) {
                error = std::format("pass '{}' 依赖自身", declared_passes[pass].name);
                return false;
            }
            add_edge(dependency, pass);
        }
    }

    // 稳定拓扑排序：始终优先调度声明更早的 pass，保证无依赖时保持声明顺序
    std::vector<uint32_t> ready;
    for (uint32_t pass = 0; pass < pass_count; ++pass) {
        if (indegree[pass] == 0) {
            ready.push_back(pass);
        }
    }
    std::sort(ready.begin(), ready.end(), std::greater<uint32_t>());

    while (!ready.empty()) {
        const uint32_t current = ready.back();
        ready.pop_back();
        execution_order.push_back(current);

        for (const uint32_t next : successors[current]) {
            if (--indegree[next] == 0) {
                const auto position = std::upper_bound(ready.begin(), ready.end(), next, std::greater<uint32_t>());
                ready.insert(position, next);
            }
        }
    }

    if (execution_order.size() != pass_count) {
        std::string unresolved;
        for (uint32_t pass = 0; pass < pass_count; ++pass) {
            if (indegree[pass] == 0) {
                continue;
            }
            if (!unresolved.empty()) {
                unresolved += ", ";
            }
            unresolved += declared_passes[pass].name;
        }
        error = std::format("FrameGraph 依赖存在环，无法确定执行顺序，涉及 pass: {}", unresolved);
        return false;
    }

    return true;
}

void compute_lifetimes(std::span<const uint32_t> execution_order,
                       const std::vector<PassInfo>& declared_passes,
                       std::span<ResourceInfo> resources) {
    for (ResourceInfo& resource : resources) {
        resource.first_use = invalid_index;
        resource.last_use = invalid_index;
        resource.use_count = 0;
    }

    for (uint32_t execution = 0; execution < execution_order.size(); ++execution) {
        const PassInfo& pass = declared_passes[execution_order[execution]];
        for (const ResourceAccess& access : pass.accesses) {
            if (access.resource.index >= resources.size()) {
                continue;
            }
            ResourceInfo& resource = resources[access.resource.index];
            if (resource.first_use == invalid_index) {
                resource.first_use = execution;
            }
            resource.last_use = execution;
            ++resource.use_count;
        }
    }
}

}  // namespace framegraph