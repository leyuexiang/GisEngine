#include <gtest/gtest.h>

#include "gisengine/rhi/rhi.h"

namespace gisengine::tests {

TEST(NullRenderDeviceTest, CreatesResourcesAndRejectsStaleHandles) {
    rhi::NullRenderDevice device;
    const rhi::BufferHandle original =
        device.create_buffer({.size_bytes = 64U,
                              .usage = rhi::BufferUsage::vertex | rhi::BufferUsage::copy_destination,
                              .debug_name = "原始缓冲"});

    ASSERT_TRUE(device.is_valid(original));
    ASSERT_TRUE(device.destroy(original));
    EXPECT_FALSE(device.is_valid(original));

    const rhi::BufferHandle replacement =
        device.create_buffer({.size_bytes = 32U, .usage = rhi::BufferUsage::uniform, .debug_name = "替换缓冲"});
    EXPECT_EQ(replacement.index, original.index);
    EXPECT_NE(replacement.generation, original.generation);
    EXPECT_FALSE(device.destroy(original));
}

TEST(NullRenderDeviceTest, ValidatesDescriptorsAndFrameLifecycle) {
    rhi::NullRenderDevice device;

    EXPECT_EQ(device.capabilities().backend, rhi::BackendType::null_backend);
    EXPECT_THROW(static_cast<void>(device.create_buffer({})), core::AssertionError);
    EXPECT_THROW(static_cast<void>(device.create_pipeline({})), core::AssertionError);

    const rhi::FrameContext frame = device.begin_frame();
    EXPECT_THROW(static_cast<void>(device.begin_frame()), core::AssertionError);
    const rhi::Fence fence = device.submit(frame);
    EXPECT_TRUE(device.is_fence_complete(fence));
    EXPECT_THROW(static_cast<void>(device.submit(frame)), core::AssertionError);
}

TEST(NullRenderDeviceTest, ValidatesTransitionsCopiesAndDeviceRecovery) {
    rhi::NullRenderDevice device;
    const rhi::BufferHandle source =
        device.create_buffer({.size_bytes = 32U, .usage = rhi::BufferUsage::copy_source, .debug_name = "源"});
    const rhi::BufferHandle destination =
        device.create_buffer({.size_bytes = 32U, .usage = rhi::BufferUsage::copy_destination, .debug_name = "目标"});
    rhi::FrameContext frame = device.begin_frame();

    device.transition(frame, source, rhi::ResourceState::undefined, rhi::ResourceState::copy_source);
    device.transition(frame, destination, rhi::ResourceState::undefined, rhi::ResourceState::copy_destination);
    device.copy_buffer(frame, source, destination, 16U);
    EXPECT_EQ(device.last_submitted_command_count(), 0U);
    const rhi::Fence fence = device.submit(frame);
    EXPECT_TRUE(device.is_fence_complete(fence));
    EXPECT_EQ(device.last_submitted_command_count(), 3U);

    device.simulate_device_loss();
    EXPECT_EQ(device.state(), rhi::DeviceState::lost);
    EXPECT_FALSE(device.is_valid(source));
    EXPECT_THROW(static_cast<void>(device.begin_frame()), core::AssertionError);
    device.recover();
    EXPECT_EQ(device.state(), rhi::DeviceState::ready);
    EXPECT_TRUE(device.is_valid(device.create_buffer(
        {.size_bytes = 32U, .usage = rhi::BufferUsage::copy_destination, .debug_name = "恢复后资源"})));
}

}  // namespace gisengine::tests
