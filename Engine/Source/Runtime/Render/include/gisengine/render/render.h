#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "gisengine/core/diagnostics.h"
#include "gisengine/core/entity.h"
#include "gisengine/math/math.h"
#include "gisengine/rhi/rhi.h"

namespace gisengine::render {

// 仅由游戏线程写入的渲染对象描述；数据按值复制进快照，不保留 Scene 或 ECS 的指针。
struct RenderItem {
    core::EntityHandle entity;
    math::Mat4f world_matrix{math::Mat4f::identity()};
    std::uint64_t mesh_id{0U};
    std::uint64_t material_id{0U};
    std::uint64_t sort_key{0U};
};

// 冻结后的只读渲染快照，可安全交给渲染线程或单线程 Web 发行路径。
class RenderSnapshot final {
   public:
    [[nodiscard]] std::uint64_t frame_number() const noexcept;
    [[nodiscard]] const std::vector<RenderItem>& items() const noexcept;

   private:
    RenderSnapshot(std::uint64_t frame_number, std::vector<RenderItem> items) noexcept;

    std::uint64_t frame_number_{0U};
    std::vector<RenderItem> items_;

    friend class RenderSnapshotBuilder;
};

// 快照构建器只允许在游戏线程提取阶段使用；build 后数据移动到不可变快照中。
class RenderSnapshotBuilder final {
   public:
    explicit RenderSnapshotBuilder(std::uint64_t frame_number, std::size_t expected_item_count = 0U);

    void add_item(RenderItem item);
    [[nodiscard]] RenderSnapshot build() &&;

   private:
    std::uint64_t frame_number_{0U};
    std::vector<RenderItem> items_;
};

enum class RenderResourceType : std::uint8_t {
    buffer,
    texture,
};

struct RenderResourceHandle {
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};

    [[nodiscard]] constexpr bool valid() const noexcept { return index != std::numeric_limits<std::uint32_t>::max(); }

    [[nodiscard]] constexpr bool operator==(const RenderResourceHandle&) const noexcept = default;
};

struct RenderPassHandle {
    std::uint32_t index{std::numeric_limits<std::uint32_t>::max()};

    [[nodiscard]] constexpr bool valid() const noexcept { return index != std::numeric_limits<std::uint32_t>::max(); }

    [[nodiscard]] constexpr bool operator==(const RenderPassHandle&) const noexcept = default;
};

// 描述逻辑资源的初始状态；图形后端负责将其映射到具体 GPU 资源。
struct RenderResourceDesc {
    std::string debug_name;
    RenderResourceType type{RenderResourceType::texture};
    rhi::ResourceState initial_state{rhi::ResourceState::undefined};
};

// Pass 对资源的单向访问；同一 Pass 中不能同时以读写集合声明同一个资源。
struct RenderResourceAccess {
    RenderResourceHandle resource;
    rhi::ResourceState state{rhi::ResourceState::undefined};
};

struct RenderPassDesc {
    std::string debug_name;
    std::vector<RenderResourceAccess> reads;
    std::vector<RenderResourceAccess> writes;
    std::vector<RenderPassHandle> prerequisites;
};

// RenderGraph 编译出的显式状态转换计划；后端可将其翻译为屏障、布局转换或 WebGPU 用途切换。
struct RenderBarrier {
    RenderResourceHandle resource;
    RenderPassHandle before_pass;
    RenderPassHandle pass;
    rhi::ResourceState before{rhi::ResourceState::undefined};
    rhi::ResourceState after{rhi::ResourceState::undefined};
};

class CompiledRenderGraph final {
   public:
    [[nodiscard]] const std::vector<RenderPassHandle>& execution_order() const noexcept;
    [[nodiscard]] const std::vector<RenderBarrier>& barriers() const noexcept;

   private:
    std::vector<RenderPassHandle> execution_order_;
    std::vector<RenderBarrier> barriers_;

    friend class RenderGraph;
};

// RenderGraph 只处理逻辑资源依赖和状态计划，不拥有长期 GPU 资源，也不调用具体后端。
class RenderGraph final {
   public:
    [[nodiscard]] RenderResourceHandle add_resource(RenderResourceDesc desc);
    [[nodiscard]] RenderPassHandle add_pass(RenderPassDesc desc);
    [[nodiscard]] CompiledRenderGraph compile() const;

   private:
    struct ResourceEntry {
        RenderResourceDesc desc;
    };

    struct PassEntry {
        RenderPassDesc desc;
    };

    struct DependencyPlan {
        std::vector<std::vector<std::uint32_t>> dependencies;
        std::vector<RenderBarrier> barriers;
    };

    [[nodiscard]] bool contains_resource(RenderResourceHandle handle) const noexcept;
    [[nodiscard]] bool contains_pass(RenderPassHandle handle) const noexcept;
    void validate_accesses(const RenderPassDesc& desc) const;
    // 分离依赖收集和拓扑排序，保证后端无关的编译步骤可独立演进与测试。
    [[nodiscard]] DependencyPlan collect_dependencies_and_barriers() const;
    [[nodiscard]] static std::vector<RenderPassHandle> compile_execution_order(
        const std::vector<std::vector<std::uint32_t>>& dependencies);

    std::vector<ResourceEntry> resources_;
    std::vector<PassEntry> passes_;
};

}  // namespace gisengine::render
