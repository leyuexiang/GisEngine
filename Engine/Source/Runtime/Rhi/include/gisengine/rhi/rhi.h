#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gisengine/core/diagnostics.h"

namespace gisengine::rhi {

enum class BackendType : std::uint8_t {
    null_backend,
    vulkan,
    direct3d12,
    metal,
    webgpu,
    webgl2,
};

enum class BufferUsage : std::uint32_t {
    none = 0U,
    vertex = 1U << 0U,
    index = 1U << 1U,
    uniform = 1U << 2U,
    storage = 1U << 3U,
    copy_source = 1U << 4U,
    copy_destination = 1U << 5U,
};

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage left, BufferUsage right) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

enum class TextureUsage : std::uint32_t {
    none = 0U,
    sampled = 1U << 0U,
    storage = 1U << 1U,
    color_attachment = 1U << 2U,
    depth_stencil_attachment = 1U << 3U,
    copy_source = 1U << 4U,
    copy_destination = 1U << 5U,
};

[[nodiscard]] constexpr TextureUsage operator|(TextureUsage left, TextureUsage right) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

enum class TextureFormat : std::uint8_t {
    rgba8_unorm,
    bgra8_unorm,
    rgba16_float,
    depth24_stencil8,
    depth32_float,
};

enum class ResourceState : std::uint8_t {
    undefined,
    copy_source,
    copy_destination,
    vertex_buffer,
    index_buffer,
    uniform_buffer,
    shader_read,
    shader_write,
    color_attachment,
    depth_stencil_attachment,
    present,
};

enum class DeviceState : std::uint8_t {
    ready,
    lost,
};

enum class FilterMode : std::uint8_t {
    nearest,
    linear,
};

enum class AddressMode : std::uint8_t {
    clamp_to_edge,
    repeat,
    mirror_repeat,
};

struct BufferDesc {
    std::size_t size_bytes{0U};
    BufferUsage usage{BufferUsage::none};
    std::string debug_name;
};

struct TextureDesc {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::uint32_t depth_or_layers{1U};
    std::uint32_t mip_levels{1U};
    TextureFormat format{TextureFormat::rgba8_unorm};
    TextureUsage usage{TextureUsage::none};
    std::string debug_name;
};

struct SamplerDesc {
    FilterMode min_filter{FilterMode::linear};
    FilterMode mag_filter{FilterMode::linear};
    AddressMode address_u{AddressMode::clamp_to_edge};
    AddressMode address_v{AddressMode::clamp_to_edge};
    AddressMode address_w{AddressMode::clamp_to_edge};
    std::string debug_name;
};

struct PipelineDesc {
    std::string vertex_shader;
    std::string fragment_shader;
    bool depth_test{true};
    std::string debug_name;
};

struct DeviceCapabilities {
    BackendType backend{BackendType::null_backend};
    bool supports_compute{false};
    bool supports_timestamp_queries{false};
    bool supports_indirect_draw{false};
    std::uint32_t max_texture_dimension_2d{0U};
    std::uint32_t max_color_attachments{0U};
};

template <typename Tag>
struct ResourceHandle {
    std::uint32_t index{0U};
    std::uint32_t generation{0U};

    [[nodiscard]] constexpr bool valid() const noexcept { return generation != 0U; }

    [[nodiscard]] constexpr bool operator==(const ResourceHandle&) const noexcept = default;
};

struct BufferTag final {};
struct TextureTag final {};
struct SamplerTag final {};
struct PipelineTag final {};

using BufferHandle = ResourceHandle<BufferTag>;
using TextureHandle = ResourceHandle<TextureTag>;
using SamplerHandle = ResourceHandle<SamplerTag>;
using PipelineHandle = ResourceHandle<PipelineTag>;

struct FrameContext {
    std::uint64_t frame_number{0U};
    std::uint32_t command_count{0U};

    [[nodiscard]] constexpr bool valid() const noexcept { return frame_number != 0U; }
};

struct Fence {
    std::uint64_t value{0U};

    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0U; }
};

class IRenderDevice {
   public:
    virtual ~IRenderDevice() = default;

    [[nodiscard]] virtual const DeviceCapabilities& capabilities() const noexcept = 0;
    [[nodiscard]] virtual DeviceState state() const noexcept = 0;
    [[nodiscard]] virtual BufferHandle create_buffer(const BufferDesc& desc) = 0;
    [[nodiscard]] virtual TextureHandle create_texture(const TextureDesc& desc) = 0;
    [[nodiscard]] virtual SamplerHandle create_sampler(const SamplerDesc& desc) = 0;
    [[nodiscard]] virtual PipelineHandle create_pipeline(const PipelineDesc& desc) = 0;
    virtual bool destroy(BufferHandle handle) noexcept = 0;
    virtual bool destroy(TextureHandle handle) noexcept = 0;
    virtual bool destroy(SamplerHandle handle) noexcept = 0;
    virtual bool destroy(PipelineHandle handle) noexcept = 0;
    [[nodiscard]] virtual FrameContext begin_frame() = 0;
    virtual void transition(FrameContext& frame, BufferHandle handle, ResourceState before, ResourceState after) = 0;
    virtual void transition(FrameContext& frame, TextureHandle handle, ResourceState before, ResourceState after) = 0;
    virtual void copy_buffer(FrameContext& frame, BufferHandle source, BufferHandle destination,
                             std::size_t size_bytes) = 0;
    [[nodiscard]] virtual Fence submit(FrameContext frame) = 0;
    [[nodiscard]] virtual bool is_fence_complete(Fence fence) const noexcept = 0;
    virtual void recover() = 0;
};

namespace detail {

template <typename Handle, typename Desc>
class ResourcePool final {
   public:
    [[nodiscard]] Handle create(const Desc& desc) {
        std::uint32_t index = 0U;
        if (free_indices_.empty()) {
            GISENGINE_ENSURE(slots_.size() <= std::numeric_limits<std::uint32_t>::max(), "RHI 资源索引已耗尽");
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back({.generation = 1U, .alive = true, .desc = desc});
        } else {
            index = free_indices_.back();
            free_indices_.pop_back();
            Slot& slot = slots_[index];
            slot.alive = true;
            slot.state = ResourceState::undefined;
            slot.desc = desc;
        }
        ++live_count_;
        return {.index = index, .generation = slots_[index].generation};
    }

    [[nodiscard]] bool destroy(Handle handle) noexcept {
        if (!valid(handle)) {
            return false;
        }

        Slot& slot = slots_[handle.index];
        slot.alive = false;
        if (slot.generation == std::numeric_limits<std::uint32_t>::max()) {
            slot.generation = 1U;
        } else {
            ++slot.generation;
        }
        free_indices_.push_back(handle.index);
        --live_count_;
        return true;
    }

    [[nodiscard]] bool valid(Handle handle) const noexcept {
        return handle.valid() && handle.index < slots_.size() && slots_[handle.index].alive &&
               slots_[handle.index].generation == handle.generation;
    }

    [[nodiscard]] std::size_t live_count() const noexcept { return live_count_; }

    [[nodiscard]] bool transition(Handle handle, ResourceState before, ResourceState after) noexcept {
        if (!valid(handle) || slots_[handle.index].state != before) {
            return false;
        }
        slots_[handle.index].state = after;
        return true;
    }

    [[nodiscard]] const Desc* description(Handle handle) const noexcept {
        return valid(handle) ? &slots_[handle.index].desc : nullptr;
    }

    void invalidate_all() {
        free_indices_.clear();
        free_indices_.reserve(slots_.size());
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            Slot& slot = slots_[index];
            slot.alive = false;
            if (slot.generation == std::numeric_limits<std::uint32_t>::max()) {
                slot.generation = 1U;
            } else {
                ++slot.generation;
            }
            free_indices_.push_back(static_cast<std::uint32_t>(index));
        }
        live_count_ = 0U;
    }

   private:
    struct Slot {
        std::uint32_t generation{1U};
        bool alive{false};
        ResourceState state{ResourceState::undefined};
        Desc desc{};
    };

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_indices_;
    std::size_t live_count_{0U};
};

}  // namespace detail

// Null RHI 不创建 GPU 对象，用于在无图形驱动的自动化环境验证资源与帧生命周期契约。
class NullRenderDevice final : public IRenderDevice {
   public:
    NullRenderDevice();

    [[nodiscard]] const DeviceCapabilities& capabilities() const noexcept override;
    [[nodiscard]] DeviceState state() const noexcept override;
    [[nodiscard]] BufferHandle create_buffer(const BufferDesc& desc) override;
    [[nodiscard]] TextureHandle create_texture(const TextureDesc& desc) override;
    [[nodiscard]] SamplerHandle create_sampler(const SamplerDesc& desc) override;
    [[nodiscard]] PipelineHandle create_pipeline(const PipelineDesc& desc) override;
    bool destroy(BufferHandle handle) noexcept override;
    bool destroy(TextureHandle handle) noexcept override;
    bool destroy(SamplerHandle handle) noexcept override;
    bool destroy(PipelineHandle handle) noexcept override;
    [[nodiscard]] FrameContext begin_frame() override;
    void transition(FrameContext& frame, BufferHandle handle, ResourceState before, ResourceState after) override;
    void transition(FrameContext& frame, TextureHandle handle, ResourceState before, ResourceState after) override;
    void copy_buffer(FrameContext& frame, BufferHandle source, BufferHandle destination,
                     std::size_t size_bytes) override;
    [[nodiscard]] Fence submit(FrameContext frame) override;
    [[nodiscard]] bool is_fence_complete(Fence fence) const noexcept override;
    void recover() override;

    // 仅供自动化测试模拟驱动失效；真实后端由平台回调触发相同状态迁移。
    void simulate_device_loss();

    [[nodiscard]] bool is_valid(BufferHandle handle) const noexcept;
    [[nodiscard]] bool is_valid(TextureHandle handle) const noexcept;
    [[nodiscard]] bool is_valid(SamplerHandle handle) const noexcept;
    [[nodiscard]] bool is_valid(PipelineHandle handle) const noexcept;
    [[nodiscard]] std::size_t live_resource_count() const noexcept;
    [[nodiscard]] std::uint32_t last_submitted_command_count() const noexcept;

   private:
    void ensure_recording(FrameContext& frame) const;
    void ensure_ready() const;

    DeviceCapabilities capabilities_;
    detail::ResourcePool<BufferHandle, BufferDesc> buffers_;
    detail::ResourcePool<TextureHandle, TextureDesc> textures_;
    detail::ResourcePool<SamplerHandle, SamplerDesc> samplers_;
    detail::ResourcePool<PipelineHandle, PipelineDesc> pipelines_;
    std::uint64_t current_frame_{0U};
    std::uint64_t completed_fence_{0U};
    std::uint32_t last_submitted_command_count_{0U};
    bool frame_open_{false};
    DeviceState state_{DeviceState::ready};
};

}  // namespace gisengine::rhi
