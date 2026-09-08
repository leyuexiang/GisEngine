#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gisengine::rhi {

class VulkanDevice;

// Vulkan 物理设备的最小能力摘要；不向上层泄露 Vulkan 原生句柄。
struct VulkanPhysicalDeviceInfo {
    std::string name;
    std::uint32_t api_version{0U};
    std::uint32_t vendor_id{0U};
    std::uint32_t device_id{0U};
};

// Vulkan 队列摘要；同一队列族可同时承担图形、计算和传输职责。
struct VulkanQueueInfo {
    std::uint32_t family_index{0U};
    bool available{false};
};

// 逻辑设备创建后的最小能力摘要，不向上层暴露 Vulkan 原生句柄。
struct VulkanDeviceCapabilities {
    bool available{false};
    VulkanPhysicalDeviceInfo physical_device;
    VulkanQueueInfo graphics_queue;
    VulkanQueueInfo compute_queue;
    VulkanQueueInfo transfer_queue;
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

    explicit VulkanInstance(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> state_;

    friend class VulkanDevice;
};

// Vulkan 逻辑设备的 RAII 封装；只选择可提交图形命令的物理设备。
class VulkanDevice final {
   public:
    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&& other) noexcept;
    VulkanDevice& operator=(VulkanDevice&& other) noexcept;
    ~VulkanDevice();

    // 设备保留实例的内部状态，使其可安全存活至逻辑设备销毁。
    [[nodiscard]] static std::optional<VulkanDevice> try_create(const VulkanInstance& instance);
    [[nodiscard]] const VulkanDeviceCapabilities& capabilities() const noexcept;

   private:
    struct State;

    explicit VulkanDevice(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> state_;
};

}  // namespace gisengine::rhi
