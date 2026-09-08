#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gisengine::rhi {

class VulkanDevice;
class VulkanSurface;
class VulkanSwapchain;
class VulkanTrianglePipeline;

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

// Vulkan 命令上下文能力摘要；记录命令池所属队列族和已分配的主级命令缓冲区数量。
struct VulkanCommandCapabilities {
    bool available{false};
    std::uint32_t queue_family_index{0U};
    std::uint32_t command_buffer_count{0U};
};

// Vulkan 帧同步能力摘要；首版每帧配置一组获取、渲染和在途同步对象。
struct VulkanFrameSyncCapabilities {
    bool available{false};
    std::uint32_t frame_count{0U};
};

// Vulkan 清屏颜色；数值范围为 0.0 到 1.0。
struct VulkanClearColor {
    float red{0.04F};
    float green{0.08F};
    float blue{0.16F};
    float alpha{1.0F};
};

// Vulkan 窗口表面创建选项；默认创建不可见的最小窗口，避免探测和测试弹出界面。
struct VulkanSurfaceCreateInfo {
    std::uint32_t width{1280U};
    std::uint32_t height{720U};
    bool visible{false};
};

// Vulkan 窗口表面能力摘要；当前首版仅支持 Windows 原生窗口路径。
struct VulkanSurfaceCapabilities {
    bool available{false};
    std::uint32_t width{0U};
    std::uint32_t height{0U};
};

// Vulkan 交换链能力摘要；暴露创建结果、尺寸以及已创建的镜像和图像视图数量。
struct VulkanSwapchainCapabilities {
    bool available{false};
    std::uint32_t width{0U};
    std::uint32_t height{0U};
    std::uint32_t image_count{0U};
    std::uint32_t image_view_count{0U};
};

// Vulkan 三角形管线能力摘要；首版固定为无顶点缓冲区的三顶点绘制。
struct VulkanTrianglePipelineCapabilities {
    bool available{false};
};

// 离线编译后的 SPIR-V 着色器路径；调用方负责提供同一构建配置下生成的文件。
struct VulkanShaderBinaryPaths {
    std::string vertex_shader_path;
    std::string fragment_shader_path;
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
    friend class VulkanSurface;
    friend class VulkanSwapchain;
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
    [[nodiscard]] static std::optional<VulkanDevice> try_create(const VulkanInstance& instance,
                                                                const VulkanSurface& surface);
    [[nodiscard]] const VulkanDeviceCapabilities& capabilities() const noexcept;

   private:
    struct State;

    explicit VulkanDevice(std::shared_ptr<State> state) noexcept;

    [[nodiscard]] static std::optional<VulkanDevice> try_create_impl(const VulkanInstance& instance,
                                                                     const VulkanSurface* surface);
    [[nodiscard]] static bool can_use_surface(const VulkanInstance& instance, const VulkanSurface* surface) noexcept;

    std::shared_ptr<State> state_;

    friend class VulkanSurface;
    friend class VulkanSwapchain;
    friend class VulkanCommandContext;
    friend class VulkanFrameSync;
    friend class VulkanTrianglePipeline;
};

// Vulkan 图形命令池和主级命令缓冲区的 RAII 封装。
class VulkanCommandContext final {
   public:
    VulkanCommandContext(const VulkanCommandContext&) = delete;
    VulkanCommandContext& operator=(const VulkanCommandContext&) = delete;
    VulkanCommandContext(VulkanCommandContext&& other) noexcept;
    VulkanCommandContext& operator=(VulkanCommandContext&& other) noexcept;
    ~VulkanCommandContext();

    [[nodiscard]] static std::optional<VulkanCommandContext> try_create(const VulkanDevice& device,
                                                                        std::uint32_t command_buffer_count = 1U);
    [[nodiscard]] const VulkanCommandCapabilities& capabilities() const noexcept;

   private:
    struct State;

    explicit VulkanCommandContext(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> state_;

    friend class VulkanSwapchain;
};

// Vulkan 帧同步对象的 RAII 封装；管理图像可用、渲染完成信号量和在途栅栏。
class VulkanFrameSync final {
   public:
    VulkanFrameSync(const VulkanFrameSync&) = delete;
    VulkanFrameSync& operator=(const VulkanFrameSync&) = delete;
    VulkanFrameSync(VulkanFrameSync&& other) noexcept;
    VulkanFrameSync& operator=(VulkanFrameSync&& other) noexcept;
    ~VulkanFrameSync();

    [[nodiscard]] static std::optional<VulkanFrameSync> try_create(const VulkanDevice& device,
                                                                   std::uint32_t frame_count = 1U);
    [[nodiscard]] const VulkanFrameSyncCapabilities& capabilities() const noexcept;

   private:
    struct State;

    explicit VulkanFrameSync(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> state_;

    friend class VulkanSwapchain;
};

// Windows 原生窗口对应的 Vulkan Surface（窗口表面）RAII 封装。
class VulkanSurface final {
   public:
    VulkanSurface(const VulkanSurface&) = delete;
    VulkanSurface& operator=(const VulkanSurface&) = delete;
    VulkanSurface(VulkanSurface&& other) noexcept;
    VulkanSurface& operator=(VulkanSurface&& other) noexcept;
    ~VulkanSurface();

    [[nodiscard]] static std::optional<VulkanSurface> try_create(const VulkanInstance& instance);
    [[nodiscard]] static std::optional<VulkanSurface> try_create(const VulkanInstance& instance,
                                                                 const VulkanSurfaceCreateInfo& create_info);
    [[nodiscard]] const VulkanSurfaceCapabilities& capabilities() const noexcept;
    // 处理窗口消息；窗口仍然打开时返回 true，收到关闭消息后返回 false。
    [[nodiscard]] bool process_events() const noexcept;
    // 窗口尺寸发生变化后保持为 true，直到交换链重建成功并由调用方确认。
    [[nodiscard]] bool has_pending_resize() const noexcept;
    // 最小化时客户端区域为零，调用方应暂停呈现并等待恢复为可渲染尺寸。
    [[nodiscard]] bool has_renderable_extent() const noexcept;
    // 仅应在已按当前窗口尺寸成功重建交换链后调用，避免重建失败时丢失请求。
    void acknowledge_resize() noexcept;
    [[nodiscard]] bool supports_present(const VulkanDevice& device) const noexcept;

   private:
    struct State;

    explicit VulkanSurface(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> state_;

    friend class VulkanDevice;
    friend class VulkanSwapchain;
};

// Vulkan 交换链的最小 RAII 封装；负责呈现配置协商以及交换链和图像视图生命周期。
class VulkanSwapchain final {
   public:
    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;
    VulkanSwapchain(VulkanSwapchain&& other) noexcept;
    VulkanSwapchain& operator=(VulkanSwapchain&& other) noexcept;
    ~VulkanSwapchain();

    [[nodiscard]] static std::optional<VulkanSwapchain> try_create(const VulkanDevice& device,
                                                                   const VulkanSurface& surface);
    [[nodiscard]] const VulkanSwapchainCapabilities& capabilities() const noexcept;
    // 获取或呈现返回交换链过期/次优状态时为 true；调用方应创建新的交换链和相关管线。
    [[nodiscard]] bool requires_recreation() const noexcept;
    // 记录、提交并呈现一次清屏；交换链失效或设备异常时返回 false。
    [[nodiscard]] bool present_clear(const VulkanDevice& device, VulkanCommandContext& command_context,
                                     VulkanFrameSync& frame_sync, const VulkanClearColor& color) noexcept;
    // 记录、提交并呈现一次清屏三角形；交换链失效或设备异常时返回 false。
    [[nodiscard]] bool present_triangle(const VulkanDevice& device, VulkanCommandContext& command_context,
                                        VulkanFrameSync& frame_sync, const VulkanTrianglePipeline& pipeline,
                                        const VulkanClearColor& clear_color) noexcept;

   private:
    struct State;

    explicit VulkanSwapchain(std::shared_ptr<State> state) noexcept;

    std::shared_ptr<State> state_;

    friend class VulkanTrianglePipeline;
};

// Vulkan 三角形图形管线的 RAII 封装；持有对应交换链状态以保证帧缓冲的图像视图生命周期。
class VulkanTrianglePipeline final {
   public:
    VulkanTrianglePipeline(const VulkanTrianglePipeline&) = delete;
    VulkanTrianglePipeline& operator=(const VulkanTrianglePipeline&) = delete;
    VulkanTrianglePipeline(VulkanTrianglePipeline&& other) noexcept;
    VulkanTrianglePipeline& operator=(VulkanTrianglePipeline&& other) noexcept;
    ~VulkanTrianglePipeline();

    [[nodiscard]] static std::optional<VulkanTrianglePipeline> try_create(const VulkanDevice& device,
                                                                          const VulkanSwapchain& swapchain,
                                                                          const VulkanShaderBinaryPaths& shader_paths);
    [[nodiscard]] const VulkanTrianglePipelineCapabilities& capabilities() const noexcept;

   private:
    struct State;

    explicit VulkanTrianglePipeline(std::unique_ptr<State> state) noexcept;

    std::unique_ptr<State> state_;

    friend class VulkanSwapchain;
};

}  // namespace gisengine::rhi
