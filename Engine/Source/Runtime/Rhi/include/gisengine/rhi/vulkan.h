#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gisengine::rhi {

// Vulkan 物理设备的最小能力摘要；不向上层泄露 Vulkan 原生句柄。
struct VulkanPhysicalDeviceInfo {
    std::string name;
    std::uint32_t api_version{0U};
    std::uint32_t vendor_id{0U};
    std::uint32_t device_id{0U};
};

// Vulkan 运行时探测结果，适用于启动阶段选择 RHI 后端。
struct VulkanCapabilities {
    bool available{false};
    std::uint32_t instance_api_version{0U};
    std::vector<VulkanPhysicalDeviceInfo> physical_devices;
};

// Vulkan 实例的 RAII 封装。加载器或实例创建失败时 try_create() 返回空值。
class VulkanInstance final {
   public:
    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;
    VulkanInstance(VulkanInstance&& other) noexcept;
    VulkanInstance& operator=(VulkanInstance&& other) noexcept;
    ~VulkanInstance();

    [[nodiscard]] static std::optional<VulkanInstance> try_create();
    [[nodiscard]] const VulkanCapabilities& capabilities() const noexcept;

   private:
    struct State;

    explicit VulkanInstance(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> state_;
};

}  // namespace gisengine::rhi
