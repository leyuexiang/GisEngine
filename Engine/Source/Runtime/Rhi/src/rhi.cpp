#include "gisengine/rhi/rhi.h"

namespace gisengine::rhi {

namespace {

[[nodiscard]] constexpr bool has_usage(BufferUsage usage) noexcept {
    return usage != BufferUsage::none;
}

[[nodiscard]] constexpr bool has_usage(TextureUsage usage) noexcept {
    return usage != TextureUsage::none;
}

[[nodiscard]] constexpr bool includes_usage(BufferUsage usages, BufferUsage required) noexcept {
    return (static_cast<std::uint32_t>(usages) & static_cast<std::uint32_t>(required)) ==
           static_cast<std::uint32_t>(required);
}

}  // namespace

NullRenderDevice::NullRenderDevice()
    : capabilities_{
          .backend = BackendType::null_backend,
          .supports_compute = false,
          .supports_timestamp_queries = false,
          .supports_indirect_draw = false,
          .max_texture_dimension_2d = 16384U,
          .max_color_attachments = 8U,
      } {}

const DeviceCapabilities& NullRenderDevice::capabilities() const noexcept {
    return capabilities_;
}

DeviceState NullRenderDevice::state() const noexcept {
    return state_;
}

void NullRenderDevice::ensure_ready() const {
    GISENGINE_ENSURE(state_ == DeviceState::ready, "渲染设备已丢失");
}

void NullRenderDevice::ensure_recording(FrameContext& frame) const {
    ensure_ready();
    GISENGINE_ENSURE(frame_open_, "命令必须记录在已打开的帧中");
    GISENGINE_ENSURE(frame.valid() && frame.frame_number == current_frame_, "帧上下文无效或已过期");
}

BufferHandle NullRenderDevice::create_buffer(const BufferDesc& desc) {
    ensure_ready();
    GISENGINE_ENSURE(desc.size_bytes > 0U, "Buffer 大小必须大于零");
    GISENGINE_ENSURE(has_usage(desc.usage), "Buffer 必须声明至少一种用途");
    return buffers_.create(desc);
}

TextureHandle NullRenderDevice::create_texture(const TextureDesc& desc) {
    ensure_ready();
    GISENGINE_ENSURE(desc.width > 0U && desc.height > 0U && desc.depth_or_layers > 0U, "纹理尺寸必须大于零");
    GISENGINE_ENSURE(desc.mip_levels > 0U, "纹理 mip 级数必须大于零");
    GISENGINE_ENSURE(has_usage(desc.usage), "纹理必须声明至少一种用途");
    GISENGINE_ENSURE(
        desc.width <= capabilities_.max_texture_dimension_2d && desc.height <= capabilities_.max_texture_dimension_2d,
        "纹理尺寸超过设备能力上限");
    return textures_.create(desc);
}

SamplerHandle NullRenderDevice::create_sampler(const SamplerDesc& desc) {
    ensure_ready();
    return samplers_.create(desc);
}

PipelineHandle NullRenderDevice::create_pipeline(const PipelineDesc& desc) {
    ensure_ready();
    GISENGINE_ENSURE(!desc.vertex_shader.empty(), "图形管线必须声明顶点 Shader");
    GISENGINE_ENSURE(!desc.fragment_shader.empty(), "图形管线必须声明片元 Shader");
    return pipelines_.create(desc);
}

bool NullRenderDevice::destroy(BufferHandle handle) noexcept {
    return buffers_.destroy(handle);
}

bool NullRenderDevice::destroy(TextureHandle handle) noexcept {
    return textures_.destroy(handle);
}

bool NullRenderDevice::destroy(SamplerHandle handle) noexcept {
    return samplers_.destroy(handle);
}

bool NullRenderDevice::destroy(PipelineHandle handle) noexcept {
    return pipelines_.destroy(handle);
}

FrameContext NullRenderDevice::begin_frame() {
    ensure_ready();
    GISENGINE_ENSURE(!frame_open_, "同一设备不能同时打开多个帧");
    frame_open_ = true;
    ++current_frame_;
    return {.frame_number = current_frame_};
}

void NullRenderDevice::transition(FrameContext& frame, BufferHandle handle, ResourceState before, ResourceState after) {
    ensure_recording(frame);
    GISENGINE_ENSURE(buffers_.transition(handle, before, after), "Buffer 资源状态转换不合法");
    ++frame.command_count;
}

void NullRenderDevice::transition(FrameContext& frame, TextureHandle handle, ResourceState before,
                                  ResourceState after) {
    ensure_recording(frame);
    GISENGINE_ENSURE(textures_.transition(handle, before, after), "Texture 资源状态转换不合法");
    ++frame.command_count;
}

void NullRenderDevice::copy_buffer(FrameContext& frame, BufferHandle source, BufferHandle destination,
                                   std::size_t size_bytes) {
    ensure_recording(frame);
    GISENGINE_ENSURE(buffers_.valid(source) && buffers_.valid(destination), "Buffer 句柄无效");
    const BufferDesc* source_desc = buffers_.description(source);
    const BufferDesc* destination_desc = buffers_.description(destination);
    GISENGINE_ENSURE(source_desc != nullptr && destination_desc != nullptr, "Buffer 描述不存在");
    GISENGINE_ENSURE(
        size_bytes > 0U && size_bytes <= source_desc->size_bytes && size_bytes <= destination_desc->size_bytes,
        "Buffer 拷贝范围超出资源大小");
    GISENGINE_ENSURE(includes_usage(source_desc->usage, BufferUsage::copy_source) &&
                         includes_usage(destination_desc->usage, BufferUsage::copy_destination),
                     "Buffer 未声明拷贝用途");
    GISENGINE_ENSURE(buffers_.transition(source, ResourceState::copy_source, ResourceState::copy_source),
                     "源 Buffer 必须处于 copy_source 状态");
    GISENGINE_ENSURE(buffers_.transition(destination, ResourceState::copy_destination, ResourceState::copy_destination),
                     "目标 Buffer 必须处于 copy_destination 状态");
    ++frame.command_count;
}

Fence NullRenderDevice::submit(FrameContext frame) {
    ensure_recording(frame);
    last_submitted_command_count_ = frame.command_count;
    frame_open_ = false;
    completed_fence_ = frame.frame_number;
    return {.value = frame.frame_number};
}

bool NullRenderDevice::is_fence_complete(Fence fence) const noexcept {
    return fence.valid() && fence.value <= completed_fence_;
}

void NullRenderDevice::recover() {
    state_ = DeviceState::ready;
    frame_open_ = false;
    completed_fence_ = current_frame_;
}

void NullRenderDevice::simulate_device_loss() {
    state_ = DeviceState::lost;
    frame_open_ = false;
    buffers_.invalidate_all();
    textures_.invalidate_all();
    samplers_.invalidate_all();
    pipelines_.invalidate_all();
}

bool NullRenderDevice::is_valid(BufferHandle handle) const noexcept {
    return buffers_.valid(handle);
}

bool NullRenderDevice::is_valid(TextureHandle handle) const noexcept {
    return textures_.valid(handle);
}

bool NullRenderDevice::is_valid(SamplerHandle handle) const noexcept {
    return samplers_.valid(handle);
}

bool NullRenderDevice::is_valid(PipelineHandle handle) const noexcept {
    return pipelines_.valid(handle);
}

std::size_t NullRenderDevice::live_resource_count() const noexcept {
    return buffers_.live_count() + textures_.live_count() + samplers_.live_count() + pipelines_.live_count();
}

std::uint32_t NullRenderDevice::last_submitted_command_count() const noexcept {
    return last_submitted_command_count_;
}

}  // namespace gisengine::rhi
