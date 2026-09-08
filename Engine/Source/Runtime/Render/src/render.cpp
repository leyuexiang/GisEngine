#include "gisengine/render/render.h"

#include <algorithm>
#include <queue>
#include <utility>
#include <vector>

namespace gisengine::render {
namespace {

[[nodiscard]] bool is_valid_access_state(rhi::ResourceState state) noexcept {
    return state != rhi::ResourceState::undefined;
}

[[nodiscard]] bool render_item_less(const RenderItem& left, const RenderItem& right) noexcept {
    if (left.sort_key != right.sort_key) {
        return left.sort_key < right.sort_key;
    }
    if (left.material_id != right.material_id) {
        return left.material_id < right.material_id;
    }
    if (left.mesh_id != right.mesh_id) {
        return left.mesh_id < right.mesh_id;
    }
    if (left.entity.index != right.entity.index) {
        return left.entity.index < right.entity.index;
    }
    return left.entity.generation < right.entity.generation;
}

void append_unique_dependency(std::vector<std::vector<std::uint32_t>>& dependencies, std::uint32_t pass,
                              std::uint32_t prerequisite) {
    if (pass == prerequisite) {
        return;
    }
    auto& pass_dependencies = dependencies[pass];
    if (std::ranges::find(pass_dependencies, prerequisite) == pass_dependencies.end()) {
        pass_dependencies.push_back(prerequisite);
    }
}

// 收集一次编译内的短生命周期状态，避免将本帧计划泄漏到长期渲染资源中。
struct CompilationState final {
    std::vector<std::vector<std::uint32_t>> dependencies;
    std::vector<std::uint32_t> last_access;
    std::vector<rhi::ResourceState> current_states;
    std::vector<RenderBarrier> barriers;
};

void schedule_access(CompilationState& compilation, std::uint32_t pass_index, const RenderResourceAccess& access) {
    const std::uint32_t resource_index = access.resource.index;
    const bool resource_was_accessed =
        compilation.last_access[resource_index] != std::numeric_limits<std::uint32_t>::max();
    if (resource_was_accessed) {
        append_unique_dependency(compilation.dependencies, pass_index, compilation.last_access[resource_index]);
    }

    const rhi::ResourceState previous_state = compilation.current_states[resource_index];
    if (previous_state != access.state) {
        const RenderPassHandle before_pass = resource_was_accessed
                                                 ? RenderPassHandle{.index = compilation.last_access[resource_index]}
                                                 : RenderPassHandle{};
        compilation.barriers.push_back({
            .resource = access.resource,
            .before_pass = before_pass,
            .pass = {.index = pass_index},
            .before = previous_state,
            .after = access.state,
        });
        compilation.current_states[resource_index] = access.state;
    }
    compilation.last_access[resource_index] = pass_index;
}

}  // namespace

RenderSnapshot::RenderSnapshot(std::uint64_t frame_number, std::vector<RenderItem> items) noexcept
    : frame_number_(frame_number), items_(std::move(items)) {}

std::uint64_t RenderSnapshot::frame_number() const noexcept {
    return frame_number_;
}

const std::vector<RenderItem>& RenderSnapshot::items() const noexcept {
    return items_;
}

RenderSnapshotBuilder::RenderSnapshotBuilder(std::uint64_t frame_number, std::size_t expected_item_count)
    : frame_number_(frame_number) {
    GISENGINE_ENSURE(frame_number_ > 0U, "渲染快照帧号必须大于零");
    items_.reserve(expected_item_count);
}

void RenderSnapshotBuilder::add_item(RenderItem item) {
    GISENGINE_ENSURE(item.entity.valid(), "渲染对象必须引用有效实体句柄");
    GISENGINE_ENSURE(item.mesh_id > 0U, "渲染对象必须声明网格标识");
    GISENGINE_ENSURE(item.material_id > 0U, "渲染对象必须声明材质标识");
    items_.push_back(item);
}

RenderSnapshot RenderSnapshotBuilder::build() && {
    std::ranges::sort(items_, render_item_less);
    return RenderSnapshot{frame_number_, std::move(items_)};
}

const std::vector<RenderPassHandle>& CompiledRenderGraph::execution_order() const noexcept {
    return execution_order_;
}

const std::vector<RenderBarrier>& CompiledRenderGraph::barriers() const noexcept {
    return barriers_;
}

RenderResourceHandle RenderGraph::add_resource(RenderResourceDesc desc) {
    GISENGINE_ENSURE(!desc.debug_name.empty(), "渲染图资源必须声明调试名称");
    GISENGINE_ENSURE(resources_.size() < std::numeric_limits<std::uint32_t>::max(), "渲染图资源索引已耗尽");
    const auto index = static_cast<std::uint32_t>(resources_.size());
    resources_.push_back({.desc = std::move(desc)});
    return {.index = index};
}

RenderPassHandle RenderGraph::add_pass(RenderPassDesc desc) {
    GISENGINE_ENSURE(!desc.debug_name.empty(), "渲染图 Pass 必须声明调试名称");
    GISENGINE_ENSURE(passes_.size() < std::numeric_limits<std::uint32_t>::max(), "渲染图 Pass 索引已耗尽");
    validate_accesses(desc);
    for (const RenderPassHandle prerequisite : desc.prerequisites) {
        GISENGINE_ENSURE(contains_pass(prerequisite), "渲染图 Pass 依赖必须在当前 Pass 之前注册");
    }
    const auto index = static_cast<std::uint32_t>(passes_.size());
    passes_.push_back({.desc = std::move(desc)});
    return {.index = index};
}

CompiledRenderGraph RenderGraph::compile() const {
    CompiledRenderGraph result;
    DependencyPlan plan = collect_dependencies_and_barriers();
    result.execution_order_ = compile_execution_order(plan.dependencies);
    result.barriers_ = std::move(plan.barriers);
    return result;
}

RenderGraph::DependencyPlan RenderGraph::collect_dependencies_and_barriers() const {
    CompilationState compilation{
        .dependencies = std::vector<std::vector<std::uint32_t>>(passes_.size()),
        .last_access = std::vector<std::uint32_t>(resources_.size(), std::numeric_limits<std::uint32_t>::max()),
        .current_states = {},
        .barriers = {},
    };
    compilation.current_states.reserve(resources_.size());
    for (const ResourceEntry& resource : resources_) {
        compilation.current_states.push_back(resource.desc.initial_state);
    }

    for (std::uint32_t pass_index{0U}; pass_index < passes_.size(); ++pass_index) {
        const PassEntry& pass = passes_[pass_index];
        for (const RenderPassHandle prerequisite : pass.desc.prerequisites) {
            append_unique_dependency(compilation.dependencies, pass_index, prerequisite.index);
        }

        for (const RenderResourceAccess& access : pass.desc.reads) {
            schedule_access(compilation, pass_index, access);
        }
        for (const RenderResourceAccess& access : pass.desc.writes) {
            schedule_access(compilation, pass_index, access);
        }
    }

    return {
        .dependencies = std::move(compilation.dependencies),
        .barriers = std::move(compilation.barriers),
    };
}

std::vector<RenderPassHandle> RenderGraph::compile_execution_order(
    const std::vector<std::vector<std::uint32_t>>& dependencies) {
    std::vector<std::vector<std::uint32_t>> successors(dependencies.size());
    std::vector<std::uint32_t> dependency_counts(dependencies.size(), 0U);
    for (std::uint32_t pass_index{0U}; pass_index < dependencies.size(); ++pass_index) {
        dependency_counts[pass_index] = static_cast<std::uint32_t>(dependencies[pass_index].size());
        for (const std::uint32_t prerequisite : dependencies[pass_index]) {
            successors[prerequisite].push_back(pass_index);
        }
    }

    std::queue<std::uint32_t> ready_passes;
    for (std::uint32_t pass_index{0U}; pass_index < dependency_counts.size(); ++pass_index) {
        if (dependency_counts[pass_index] == 0U) {
            ready_passes.push(pass_index);
        }
    }
    std::vector<RenderPassHandle> execution_order;
    execution_order.reserve(dependencies.size());
    while (!ready_passes.empty()) {
        const std::uint32_t pass_index = ready_passes.front();
        ready_passes.pop();
        execution_order.push_back({.index = pass_index});
        for (const std::uint32_t successor : successors[pass_index]) {
            --dependency_counts[successor];
            if (dependency_counts[successor] == 0U) {
                ready_passes.push(successor);
            }
        }
    }

    GISENGINE_ENSURE(execution_order.size() == dependencies.size(), "渲染图存在循环依赖");
    return execution_order;
}

bool RenderGraph::contains_resource(RenderResourceHandle handle) const noexcept {
    return handle.valid() && handle.index < resources_.size();
}

bool RenderGraph::contains_pass(RenderPassHandle handle) const noexcept {
    return handle.valid() && handle.index < passes_.size();
}

void RenderGraph::validate_accesses(const RenderPassDesc& desc) const {
    std::vector<RenderResourceHandle> declared_resources;
    declared_resources.reserve(desc.reads.size() + desc.writes.size());
    const auto validate = [this, &declared_resources](const RenderResourceAccess& access) {
        GISENGINE_ENSURE(contains_resource(access.resource), "渲染图 Pass 引用了未注册资源");
        GISENGINE_ENSURE(is_valid_access_state(access.state), "渲染图资源访问必须声明目标状态");
        const auto duplicate = std::ranges::find(declared_resources, access.resource);
        GISENGINE_ENSURE(duplicate == declared_resources.end(), "同一 Pass 不能重复声明同一个资源访问");
        declared_resources.push_back(access.resource);
    };
    for (const RenderResourceAccess& access : desc.reads) {
        validate(access);
    }
    for (const RenderResourceAccess& access : desc.writes) {
        validate(access);
    }
}

}  // namespace gisengine::render
