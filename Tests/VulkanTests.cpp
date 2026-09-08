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

}  // namespace gisengine::tests
