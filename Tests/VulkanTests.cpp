#include <gtest/gtest.h>

#include <algorithm>
#include <utility>

#include "gisengine/rhi/vulkan.h"

namespace gisengine::tests {
namespace {

[[nodiscard]] bool all_devices_have_identity(const rhi::VulkanCapabilities& capabilities) {
    return std::ranges::all_of(capabilities.physical_devices, [](const rhi::VulkanPhysicalDeviceInfo& device) {
        return !device.name.empty() && device.vendor_id != 0U;
    });
}

}  // namespace

TEST(VulkanInstanceTest, ReportsRuntimeAndPhysicalDevicesWhenAvailable) {
    const auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }

    const auto& capabilities = instance->capabilities();
    EXPECT_TRUE(capabilities.available);
    EXPECT_GE(capabilities.instance_api_version, 1U << 22U);
    ASSERT_FALSE(capabilities.physical_devices.empty());
    EXPECT_TRUE(all_devices_have_identity(capabilities));
}

TEST(VulkanInstanceTest, MovedInstanceKeepsCapabilities) {
    auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }

    const auto expected_device_count = instance->capabilities().physical_devices.size();
    auto moved = std::move(*instance);
    EXPECT_EQ(moved.capabilities().physical_devices.size(), expected_device_count);
    EXPECT_TRUE(moved.capabilities().available);
}

TEST(VulkanDeviceTest, CreatesLogicalDeviceWithGraphicsQueue) {
    const auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }

    const auto device = rhi::VulkanDevice::try_create(*instance);
    if (!device.has_value()) {
        GTEST_SKIP() << "当前环境没有支持图形队列的 Vulkan 物理设备";
    }

    const auto& capabilities = device->capabilities();
    EXPECT_TRUE(capabilities.available);
    EXPECT_FALSE(capabilities.physical_device.name.empty());
    EXPECT_TRUE(capabilities.graphics_queue.available);
}

TEST(VulkanDeviceTest, KeepsInstanceAliveAfterCallerReleasesIt) {
    auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }

    auto device = rhi::VulkanDevice::try_create(*instance);
    if (!device.has_value()) {
        GTEST_SKIP() << "当前环境没有支持图形队列的 Vulkan 物理设备";
    }

    const auto graphics_family = device->capabilities().graphics_queue.family_index;
    instance.reset();
    auto moved_device = std::move(*device);
    EXPECT_TRUE(moved_device.capabilities().available);
    EXPECT_TRUE(moved_device.capabilities().graphics_queue.available);
    EXPECT_EQ(moved_device.capabilities().graphics_queue.family_index, graphics_family);
}

TEST(VulkanCommandContextTest, CreatesPrimaryCommandBuffers) {
    const auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }
    const auto device = rhi::VulkanDevice::try_create(*instance);
    if (!device.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建图形命令的 Vulkan 逻辑设备";
    }

    const auto command_context = rhi::VulkanCommandContext::try_create(*device, 2U);
    if (!command_context.has_value()) {
        ADD_FAILURE() << "有效 Vulkan 逻辑设备应能创建图形命令池和主级命令缓冲区";
        return;
    }

    const auto& capabilities = command_context->capabilities();
    EXPECT_TRUE(capabilities.available);
    EXPECT_EQ(capabilities.queue_family_index, device->capabilities().graphics_queue.family_index);
    EXPECT_EQ(capabilities.command_buffer_count, 2U);
}

TEST(VulkanCommandContextTest, KeepsCommandPoolAliveAfterDeviceMove) {
    auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }
    auto device = rhi::VulkanDevice::try_create(*instance);
    if (!device.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建图形命令的 Vulkan 逻辑设备";
    }
    auto command_context = rhi::VulkanCommandContext::try_create(*device);
    if (!command_context.has_value()) {
        ADD_FAILURE() << "有效 Vulkan 逻辑设备应能创建图形命令池和主级命令缓冲区";
        return;
    }

    const auto expected_family = command_context->capabilities().queue_family_index;
    instance.reset();
    device.reset();
    auto moved = std::move(*command_context);
    EXPECT_TRUE(moved.capabilities().available);
    EXPECT_EQ(moved.capabilities().queue_family_index, expected_family);
    EXPECT_EQ(moved.capabilities().command_buffer_count, 1U);
}

TEST(VulkanFrameSyncTest, CreatesSynchronizationObjectsForEachFrame) {
    const auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }
    const auto device = rhi::VulkanDevice::try_create(*instance);
    if (!device.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建帧同步对象的 Vulkan 逻辑设备";
    }

    const auto frame_sync = rhi::VulkanFrameSync::try_create(*device, 2U);
    if (!frame_sync.has_value()) {
        ADD_FAILURE() << "有效 Vulkan 逻辑设备应能创建每帧同步对象";
        return;
    }

    EXPECT_TRUE(frame_sync->capabilities().available);
    EXPECT_EQ(frame_sync->capabilities().frame_count, 2U);
}

TEST(VulkanFrameSyncTest, KeepsSynchronizationObjectsAliveAfterDeviceRelease) {
    auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }
    auto device = rhi::VulkanDevice::try_create(*instance);
    if (!device.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建帧同步对象的 Vulkan 逻辑设备";
    }
    auto frame_sync = rhi::VulkanFrameSync::try_create(*device);
    if (!frame_sync.has_value()) {
        ADD_FAILURE() << "有效 Vulkan 逻辑设备应能创建每帧同步对象";
        return;
    }

    instance.reset();
    device.reset();
    auto moved = std::move(*frame_sync);
    EXPECT_TRUE(moved.capabilities().available);
    EXPECT_EQ(moved.capabilities().frame_count, 1U);
}

TEST(VulkanSurfaceTest, CreatesHiddenWindowSurfaceWhenSupported) {
    const auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }

    auto surface = rhi::VulkanSurface::try_create(*instance);
    if (!surface.has_value()) {
        GTEST_SKIP() << "当前环境不支持 Windows Vulkan Surface";
    }

    const auto& capabilities = surface->capabilities();
    ASSERT_TRUE(capabilities.available);
    EXPECT_GT(capabilities.width, 0U);
    EXPECT_GT(capabilities.height, 0U);
    EXPECT_TRUE(surface->has_renderable_extent());
    EXPECT_FALSE(surface->has_pending_resize());
}

TEST(VulkanSurfaceTest, GraphicsQueueSupportsPresentWhenSurfaceDeviceIsCreated) {
    const auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }

    const auto surface = rhi::VulkanSurface::try_create(*instance);
    if (!surface.has_value()) {
        GTEST_SKIP() << "当前环境不支持 Windows Vulkan Surface";
    }

    auto device = rhi::VulkanDevice::try_create(*instance, *surface);
    if (!device.has_value()) {
        GTEST_SKIP() << "当前环境没有同时支持图形和呈现队列的 Vulkan 物理设备";
    }

    EXPECT_TRUE(surface->supports_present(*device));
    EXPECT_TRUE(device->capabilities().graphics_queue.available);
}

TEST(VulkanSurfaceTest, MovedSurfaceKeepsCapabilities) {
    auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }

    auto surface = rhi::VulkanSurface::try_create(*instance);
    if (!surface.has_value()) {
        GTEST_SKIP() << "当前环境不支持 Windows Vulkan Surface";
    }

    const auto expected_width = surface->capabilities().width;
    const auto expected_height = surface->capabilities().height;
    auto moved = std::move(*surface);
    EXPECT_TRUE(moved.capabilities().available);
    EXPECT_EQ(moved.capabilities().width, expected_width);
    EXPECT_EQ(moved.capabilities().height, expected_height);
}

TEST(VulkanSurfaceTest, ProcessesEventsWhileWindowIsOpen) {
    auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }

    auto surface = rhi::VulkanSurface::try_create(*instance);
    if (!surface.has_value()) {
        GTEST_SKIP() << "当前环境不支持 Windows Vulkan Surface";
    }

    EXPECT_TRUE(surface->process_events());
}

TEST(VulkanSwapchainTest, CreatesSwapchainWhenSurfaceIsSupported) {
    const auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }
    const auto surface = rhi::VulkanSurface::try_create(*instance);
    if (!surface.has_value()) {
        GTEST_SKIP() << "当前环境不支持 Windows Vulkan Surface";
    }
    const auto device = rhi::VulkanDevice::try_create(*instance, *surface);
    if (!device.has_value()) {
        GTEST_SKIP() << "当前环境没有同时支持图形和呈现队列的 Vulkan 物理设备";
    }
    const auto swapchain = rhi::VulkanSwapchain::try_create(*device, *surface);
    if (!swapchain.has_value()) {
        ADD_FAILURE() << "已支持交换链扩展的设备应能为有效 Surface 创建交换链";
        return;
    }

    const auto& capabilities = swapchain->capabilities();
    EXPECT_TRUE(capabilities.available);
    EXPECT_GT(capabilities.width, 0U);
    EXPECT_GT(capabilities.height, 0U);
    EXPECT_GE(capabilities.image_count, 1U);
    EXPECT_EQ(capabilities.image_view_count, capabilities.image_count);
}

TEST(VulkanSwapchainTest, KeepsCapabilitiesAfterMove) {
    auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }
    auto surface = rhi::VulkanSurface::try_create(*instance);
    if (!surface.has_value()) {
        GTEST_SKIP() << "当前环境不支持 Windows Vulkan Surface";
    }
    auto device = rhi::VulkanDevice::try_create(*instance, *surface);
    if (!device.has_value()) {
        GTEST_SKIP() << "当前环境没有同时支持图形和呈现队列的 Vulkan 物理设备";
    }
    auto swapchain = rhi::VulkanSwapchain::try_create(*device, *surface);
    if (!swapchain.has_value()) {
        ADD_FAILURE() << "已支持交换链扩展的设备应能为有效 Surface 创建交换链";
        return;
    }
    const auto expected_count = swapchain->capabilities().image_count;
    const auto expected_view_count = swapchain->capabilities().image_view_count;
    instance.reset();
    device.reset();
    surface.reset();
    auto moved = std::move(*swapchain);
    EXPECT_TRUE(moved.capabilities().available);
    EXPECT_EQ(moved.capabilities().image_count, expected_count);
    EXPECT_EQ(moved.capabilities().image_view_count, expected_view_count);
    EXPECT_EQ(moved.capabilities().image_view_count, moved.capabilities().image_count);
}

TEST(VulkanClearTest, PresentsClearColorToHiddenSurface) {
    const auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }
    const auto surface = rhi::VulkanSurface::try_create(*instance);
    if (!surface.has_value()) {
        GTEST_SKIP() << "当前环境不支持 Windows Vulkan Surface";
    }
    const auto device = rhi::VulkanDevice::try_create(*instance, *surface);
    if (!device.has_value()) {
        GTEST_SKIP() << "当前环境没有同时支持图形和呈现队列的 Vulkan 物理设备";
    }
    auto swapchain = rhi::VulkanSwapchain::try_create(*device, *surface);
    auto command_context = rhi::VulkanCommandContext::try_create(*device);
    auto frame_sync = rhi::VulkanFrameSync::try_create(*device);
    if (!swapchain.has_value() || !command_context.has_value() || !frame_sync.has_value()) {
        ADD_FAILURE() << "有效 Vulkan 呈现设备应能完成清屏所需的资源创建";
        return;
    }

    EXPECT_TRUE(
        swapchain->present_clear(*device, *command_context, *frame_sync,
                                 rhi::VulkanClearColor{.red = 0.1F, .green = 0.2F, .blue = 0.3F, .alpha = 1.0F}));
}

#if defined(GISENGINE_TRIANGLE_VERTEX_SHADER_PATH) && defined(GISENGINE_TRIANGLE_FRAGMENT_SHADER_PATH)
TEST(VulkanTrianglePipelineTest, CreatesAndPresentsTriangleToHiddenSurface) {
    const auto instance = rhi::VulkanInstance::try_create();
    if (!instance.has_value()) {
        GTEST_SKIP() << "当前环境没有可创建的 Vulkan 实例或物理设备";
    }
    const auto surface = rhi::VulkanSurface::try_create(*instance);
    if (!surface.has_value()) {
        GTEST_SKIP() << "当前环境不支持 Windows Vulkan Surface";
    }
    const auto device = rhi::VulkanDevice::try_create(*instance, *surface);
    if (!device.has_value()) {
        GTEST_SKIP() << "当前环境没有同时支持图形和呈现队列的 Vulkan 物理设备";
    }
    auto swapchain = rhi::VulkanSwapchain::try_create(*device, *surface);
    auto command_context = rhi::VulkanCommandContext::try_create(*device);
    auto frame_sync = rhi::VulkanFrameSync::try_create(*device);
    if (!swapchain.has_value() || !command_context.has_value() || !frame_sync.has_value()) {
        ADD_FAILURE() << "有效 Vulkan 呈现设备应能创建三角形绘制所需的基础资源";
        return;
    }

    auto pipeline =
        rhi::VulkanTrianglePipeline::try_create(*device, *swapchain,
                                                rhi::VulkanShaderBinaryPaths{
                                                    .vertex_shader_path = GISENGINE_TRIANGLE_VERTEX_SHADER_PATH,
                                                    .fragment_shader_path = GISENGINE_TRIANGLE_FRAGMENT_SHADER_PATH,
                                                });
    if (!pipeline.has_value()) {
        ADD_FAILURE() << "由 shaderc 生成的有效 SPIR-V 应能创建 Vulkan 三角形图形管线";
        return;
    }

    EXPECT_TRUE(pipeline->capabilities().available);
    EXPECT_TRUE(
        swapchain->present_triangle(*device, *command_context, *frame_sync, *pipeline, rhi::VulkanClearColor{}));
}
#endif

}  // namespace gisengine::tests
