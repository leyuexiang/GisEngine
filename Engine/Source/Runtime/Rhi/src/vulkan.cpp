#include "gisengine/rhi/vulkan.h"

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <fstream>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#define GISENGINE_CAN_LOAD_VULKAN 1
#elif defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#define GISENGINE_CAN_LOAD_VULKAN 1
#else
#define GISENGINE_CAN_LOAD_VULKAN 0
#endif

namespace gisengine::rhi {
namespace {

constexpr std::uint32_t k_no_queue_family{std::numeric_limits<std::uint32_t>::max()};
#ifdef _WIN32
constexpr wchar_t k_vulkan_loader_file_name[]{L"\\vulkan-1.dll"};
constexpr wchar_t k_surface_window_class_prefix[]{L"GisEngineVulkanSurface"};

// 事件状态单独分配，确保窗口过程在 HiddenWindow 移动后仍指向有效对象。
struct WindowEventState {
    std::atomic_uint32_t client_width{0U};
    std::atomic_uint32_t client_height{0U};
    std::atomic_bool resize_pending{false};
};

struct WindowExtent {
    std::uint32_t width{0U};
    std::uint32_t height{0U};
};

class HiddenWindow final {
   public:
    HiddenWindow() = default;
    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;

    HiddenWindow(HiddenWindow&& other) noexcept
        : instance_(std::exchange(other.instance_, nullptr)),
          window_(std::exchange(other.window_, nullptr)),
          class_name_(std::move(other.class_name_)),
          event_state_(std::move(other.event_state_)) {}

    HiddenWindow& operator=(HiddenWindow&& other) noexcept {
        if (this != &other) {
            reset();
            instance_ = std::exchange(other.instance_, nullptr);
            window_ = std::exchange(other.window_, nullptr);
            class_name_ = std::move(other.class_name_);
            event_state_ = std::move(other.event_state_);
        }
        return *this;
    }

    ~HiddenWindow() { reset(); }

    [[nodiscard]] static std::optional<HiddenWindow> try_create(std::uint32_t width, std::uint32_t height,
                                                                bool visible) {
        HiddenWindow result;
        result.event_state_ = std::make_shared<WindowEventState>();
        result.event_state_->client_width.store(width, std::memory_order_relaxed);
        result.event_state_->client_height.store(height, std::memory_order_relaxed);
        result.instance_ = GetModuleHandleW(nullptr);
        if (result.instance_ == nullptr) {
            return std::nullopt;
        }

        static std::atomic_uint64_t next_id{0U};
        result.class_name_ = k_surface_window_class_prefix;
        result.class_name_ += std::to_wstring(next_id.fetch_add(1U, std::memory_order_relaxed));
        const WNDCLASSEXW window_class{
            .cbSize = sizeof(WNDCLASSEXW),
            .style = 0U,
            .lpfnWndProc = window_proc,
            .cbClsExtra = 0,
            .cbWndExtra = 0,
            .hInstance = result.instance_,
            .hIcon = nullptr,
            .hCursor = nullptr,
            .hbrBackground = nullptr,
            .lpszMenuName = nullptr,
            .lpszClassName = result.class_name_.c_str(),
            .hIconSm = nullptr,
        };
        if (RegisterClassExW(&window_class) == 0U) {
            return std::nullopt;
        }

        // 尺寸变化由事件状态记录；调用方会在下一帧按最新客户端尺寸替换交换链和管线。
        const DWORD window_style = visible ? (WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_MAXIMIZEBOX) : WS_POPUP;
        RECT window_rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        if (visible && AdjustWindowRectEx(&window_rect, window_style, FALSE, 0U) == FALSE) {
            UnregisterClassW(result.class_name_.c_str(), result.instance_);
            return std::nullopt;
        }
        result.window_ = CreateWindowExW(0U, result.class_name_.c_str(), L"GisEngine Vulkan Surface", window_style,
                                         visible ? CW_USEDEFAULT : 0, visible ? CW_USEDEFAULT : 0,
                                         window_rect.right - window_rect.left, window_rect.bottom - window_rect.top,
                                         nullptr, nullptr, result.instance_, result.event_state_.get());
        if (result.window_ == nullptr) {
            UnregisterClassW(result.class_name_.c_str(), result.instance_);
            return std::nullopt;
        }
        if (visible) {
            ShowWindow(result.window_, SW_SHOW);
            UpdateWindow(result.window_);
        }
        // 创建窗口阶段也会接收到 WM_SIZE；初始尺寸无需触发一次资源重建。
        result.acknowledge_resize();
        return result;
    }

    [[nodiscard]] bool process_events() const noexcept {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
            if (message.message == WM_QUIT) {
                return false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return window_ != nullptr && IsWindow(window_) != FALSE;
    }

    [[nodiscard]] HINSTANCE instance() const noexcept { return instance_; }
    [[nodiscard]] HWND window() const noexcept { return window_; }
    [[nodiscard]] bool has_pending_resize() const noexcept {
        return event_state_ != nullptr && event_state_->resize_pending.load(std::memory_order_acquire);
    }
    [[nodiscard]] WindowExtent client_extent() const noexcept {
        if (event_state_ == nullptr) {
            return {};
        }
        return {
            .width = event_state_->client_width.load(std::memory_order_acquire),
            .height = event_state_->client_height.load(std::memory_order_acquire),
        };
    }
    [[nodiscard]] bool has_renderable_extent() const noexcept {
        const WindowExtent extent = client_extent();
        return extent.width > 0U && extent.height > 0U;
    }
    void acknowledge_resize() noexcept {
        if (event_state_ != nullptr) {
            event_state_->resize_pending.store(false, std::memory_order_release);
        }
    }

   private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) noexcept {
        if (message == WM_NCCREATE) {
            const auto* create_info =
                reinterpret_cast<const CREATESTRUCTW*>(l_param);  // NOLINT(performance-no-int-to-ptr)
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create_info->lpCreateParams));
            return TRUE;
        }
        if (message == WM_SIZE) {
            auto* event_state = reinterpret_cast<WindowEventState*>(  // NOLINT(performance-no-int-to-ptr)
                GetWindowLongPtrW(window, GWLP_USERDATA));
            if (event_state != nullptr) {
                event_state->client_width.store(static_cast<std::uint32_t>(LOWORD(l_param)), std::memory_order_relaxed);
                event_state->client_height.store(static_cast<std::uint32_t>(HIWORD(l_param)),
                                                 std::memory_order_relaxed);
                event_state->resize_pending.store(true, std::memory_order_release);
            }
            return 0;
        }
        if (message == WM_DESTROY) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, w_param, l_param);
    }

    void reset() noexcept {
        if (window_ != nullptr) {
            DestroyWindow(window_);
            window_ = nullptr;
        }
        if (instance_ != nullptr && !class_name_.empty()) {
            UnregisterClassW(class_name_.c_str(), instance_);
        }
        instance_ = nullptr;
        class_name_.clear();
        event_state_.reset();
    }

    HINSTANCE instance_{nullptr};
    HWND window_{nullptr};
    std::wstring class_name_;
    std::shared_ptr<WindowEventState> event_state_;
};
#endif

class VulkanLoader final {
   public:
    VulkanLoader() noexcept {
#ifdef _WIN32
        // 限定从系统目录加载，避免当前工作目录中的同名 DLL 参与加载搜索路径。
        std::array<wchar_t, MAX_PATH> library_path{};
        const auto directory_length = GetSystemDirectoryW(library_path.data(), static_cast<UINT>(library_path.size()));
        constexpr std::size_t file_name_size{sizeof(k_vulkan_loader_file_name) / sizeof(k_vulkan_loader_file_name[0])};
        if (directory_length > 0U &&
            static_cast<std::size_t>(directory_length) + file_name_size <= library_path.size()) {
            std::copy(std::begin(k_vulkan_loader_file_name), std::end(k_vulkan_loader_file_name),
                      library_path.begin() + static_cast<std::ptrdiff_t>(directory_length));
            handle_ = static_cast<void*>(LoadLibraryExW(library_path.data(), nullptr, 0U));
        }
#elif defined(__APPLE__)
        handle_ = dlopen("libvulkan.1.dylib", RTLD_LOCAL | RTLD_NOW);
#elif GISENGINE_CAN_LOAD_VULKAN
        handle_ = dlopen("libvulkan.so.1", RTLD_LOCAL | RTLD_NOW);
#endif
        if (handle_ != nullptr) {
            get_instance_proc_addr_ = load<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
        }
    }

    VulkanLoader(const VulkanLoader&) = delete;
    VulkanLoader& operator=(const VulkanLoader&) = delete;

    VulkanLoader(VulkanLoader&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)),
          get_instance_proc_addr_(std::exchange(other.get_instance_proc_addr_, nullptr)) {}

    VulkanLoader& operator=(VulkanLoader&& other) noexcept {
        if (this != &other) {
            close();
            handle_ = std::exchange(other.handle_, nullptr);
            get_instance_proc_addr_ = std::exchange(other.get_instance_proc_addr_, nullptr);
        }
        return *this;
    }

    ~VulkanLoader() { close(); }

    [[nodiscard]] explicit operator bool() const noexcept {
        return handle_ != nullptr && get_instance_proc_addr_ != nullptr;
    }

    [[nodiscard]] PFN_vkGetInstanceProcAddr get_instance_proc_addr() const noexcept { return get_instance_proc_addr_; }

   private:
    template <typename Function>
    [[nodiscard]] Function load(const char* name) const noexcept {
        if (handle_ == nullptr) {
            return nullptr;
        }
#ifdef _WIN32
        const auto symbol = GetProcAddress(static_cast<HMODULE>(handle_), name);
        static_assert(sizeof(Function) == sizeof(symbol), "Vulkan 函数指针 ABI 大小不一致");
        // Windows ABI 规定 FARPROC 与 Vulkan 的全局入口点使用同一函数指针表示；此处仅用于动态库符号解析。
        return std::bit_cast<Function>(symbol);  // NOLINT(bugprone-bitwise-pointer-cast)
#elif GISENGINE_CAN_LOAD_VULKAN
        return reinterpret_cast<Function>(dlsym(handle_, name));
#else
        static_cast<void>(name);
        return nullptr;
#endif
    }

    void close() noexcept {
        if (handle_ == nullptr) {
            return;
        }
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle_));
#elif GISENGINE_CAN_LOAD_VULKAN
        dlclose(handle_);
#endif
        handle_ = nullptr;
        get_instance_proc_addr_ = nullptr;
    }

    void* handle_{nullptr};
    PFN_vkGetInstanceProcAddr get_instance_proc_addr_{nullptr};
};

template <typename Function>
[[nodiscard]] Function load_instance_function(PFN_vkGetInstanceProcAddr get_instance_proc_addr, VkInstance instance,
                                              const char* name) noexcept {
    if (get_instance_proc_addr == nullptr) {
        return nullptr;
    }
    const auto symbol = get_instance_proc_addr(instance, name);
    static_assert(sizeof(Function) == sizeof(symbol), "Vulkan 实例函数指针 ABI 大小不一致");
    // Vulkan 加载器以 PFN_vkVoidFunction 返回指定函数；目标类型由官方 Vulkan-Headers 的 PFN 类型约束。
    return std::bit_cast<Function>(symbol);  // NOLINT(bugprone-bitwise-pointer-cast)
}

template <typename Function>
[[nodiscard]] Function load_device_function(PFN_vkGetDeviceProcAddr get_device_proc_addr, VkDevice device,
                                            const char* name) noexcept {
    if (get_device_proc_addr == nullptr) {
        return nullptr;
    }
    const auto symbol = get_device_proc_addr(device, name);
    static_assert(sizeof(Function) == sizeof(symbol), "Vulkan 设备函数指针 ABI 大小不一致");
    // Vulkan 加载器以 PFN_vkVoidFunction 返回指定函数；目标类型由官方 Vulkan-Headers 的 PFN 类型约束。
    return std::bit_cast<Function>(symbol);  // NOLINT(bugprone-bitwise-pointer-cast)
}

[[nodiscard]] VulkanPhysicalDeviceInfo make_physical_device_info(const VkPhysicalDeviceProperties& properties) {
    return {
        .name = properties.deviceName,
        .api_version = properties.apiVersion,
        .vendor_id = properties.vendorID,
        .device_id = properties.deviceID,
    };
}

[[nodiscard]] constexpr bool supports_flags(VkQueueFlags available, VkQueueFlags required) noexcept {
    return (available & required) == required;
}

[[nodiscard]] std::optional<std::uint32_t> select_graphics_queue(const std::vector<VkQueueFamilyProperties>& families) {
    std::uint32_t selected{k_no_queue_family};
    std::uint32_t selected_score{0U};
    for (std::uint32_t index{0U}; index < families.size(); ++index) {
        const VkQueueFamilyProperties& family = families[index];
        if (family.queueCount == 0U || !supports_flags(family.queueFlags, VK_QUEUE_GRAPHICS_BIT)) {
            continue;
        }

        const std::uint32_t score{(supports_flags(family.queueFlags, VK_QUEUE_COMPUTE_BIT) ? 2U : 0U) +
                                  (supports_flags(family.queueFlags, VK_QUEUE_TRANSFER_BIT) ? 1U : 0U)};
        if (selected == k_no_queue_family || score > selected_score) {
            selected = index;
            selected_score = score;
        }
    }
    return selected == k_no_queue_family ? std::nullopt : std::optional{selected};
}

[[nodiscard]] std::optional<std::uint32_t> select_compute_queue(const std::vector<VkQueueFamilyProperties>& families,
                                                                std::uint32_t graphics_queue) {
    // 优先独立计算队列，不能独立时复用图形队列，避免为最小切片引入跨队列同步成本。
    for (std::uint32_t index{0U}; index < families.size(); ++index) {
        const VkQueueFamilyProperties& family = families[index];
        if (family.queueCount > 0U && supports_flags(family.queueFlags, VK_QUEUE_COMPUTE_BIT) &&
            !supports_flags(family.queueFlags, VK_QUEUE_GRAPHICS_BIT)) {
            return index;
        }
    }
    if (supports_flags(families[graphics_queue].queueFlags, VK_QUEUE_COMPUTE_BIT)) {
        return graphics_queue;
    }
    for (std::uint32_t index{0U}; index < families.size(); ++index) {
        const VkQueueFamilyProperties& family = families[index];
        if (family.queueCount > 0U && supports_flags(family.queueFlags, VK_QUEUE_COMPUTE_BIT)) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> select_transfer_queue(const std::vector<VkQueueFamilyProperties>& families,
                                                                 std::uint32_t graphics_queue,
                                                                 const std::optional<std::uint32_t>& compute_queue) {
    // 传输优先级从专用队列逐级回退到共享队列；后续上传通道可据此减少与图形队列的竞争。
    for (std::uint32_t index{0U}; index < families.size(); ++index) {
        const VkQueueFamilyProperties& family = families[index];
        if (family.queueCount > 0U && supports_flags(family.queueFlags, VK_QUEUE_TRANSFER_BIT) &&
            !supports_flags(family.queueFlags, VK_QUEUE_GRAPHICS_BIT) &&
            !supports_flags(family.queueFlags, VK_QUEUE_COMPUTE_BIT)) {
            return index;
        }
    }
    for (std::uint32_t index{0U}; index < families.size(); ++index) {
        const VkQueueFamilyProperties& family = families[index];
        if (family.queueCount > 0U && supports_flags(family.queueFlags, VK_QUEUE_TRANSFER_BIT) &&
            !supports_flags(family.queueFlags, VK_QUEUE_GRAPHICS_BIT)) {
            return index;
        }
    }
    if (compute_queue.has_value() && supports_flags(families[*compute_queue].queueFlags, VK_QUEUE_TRANSFER_BIT)) {
        return compute_queue;
    }
    if (supports_flags(families[graphics_queue].queueFlags, VK_QUEUE_TRANSFER_BIT)) {
        return graphics_queue;
    }
    for (std::uint32_t index{0U}; index < families.size(); ++index) {
        const VkQueueFamilyProperties& family = families[index];
        if (family.queueCount > 0U && supports_flags(family.queueFlags, VK_QUEUE_TRANSFER_BIT)) {
            return index;
        }
    }
    return std::nullopt;
}

struct QueueSelection {
    std::uint32_t graphics_queue{k_no_queue_family};
    std::optional<std::uint32_t> compute_queue;
    std::optional<std::uint32_t> transfer_queue;
};

[[nodiscard]] bool queue_supports_present(PFN_vkGetPhysicalDeviceSurfaceSupportKHR get_surface_support,
                                          VkPhysicalDevice physical_device, std::uint32_t queue_family,
                                          VkSurfaceKHR surface) noexcept {
    if (get_surface_support == nullptr) {
        return false;
    }
    VkBool32 supported{VK_FALSE};
    return get_surface_support(physical_device, queue_family, surface, &supported) == VK_SUCCESS &&
           supported == VK_TRUE;
}

[[nodiscard]] std::optional<QueueSelection> select_queues(
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queue_families, VkPhysicalDevice physical_device,
    VkSurfaceKHR surface = VK_NULL_HANDLE, PFN_vkGetPhysicalDeviceSurfaceSupportKHR get_surface_support = nullptr) {
    std::uint32_t family_count{0U};
    get_queue_families(physical_device, &family_count, nullptr);
    if (family_count == 0U) {
        return std::nullopt;
    }

    std::vector<VkQueueFamilyProperties> families(family_count);
    get_queue_families(physical_device, &family_count, families.data());
    families.resize(family_count);
    const auto graphics_queue = [&]() -> std::optional<std::uint32_t> {
        if (surface == VK_NULL_HANDLE) {
            return select_graphics_queue(families);
        }
        std::uint32_t selected{k_no_queue_family};
        std::uint32_t selected_score{0U};
        for (std::uint32_t index{0U}; index < families.size(); ++index) {
            const VkQueueFamilyProperties& family = families[index];
            if (family.queueCount == 0U || !supports_flags(family.queueFlags, VK_QUEUE_GRAPHICS_BIT) ||
                !queue_supports_present(get_surface_support, physical_device, index, surface)) {
                continue;
            }

            const std::uint32_t score{(supports_flags(family.queueFlags, VK_QUEUE_COMPUTE_BIT) ? 2U : 0U) +
                                      (supports_flags(family.queueFlags, VK_QUEUE_TRANSFER_BIT) ? 1U : 0U)};
            if (selected == k_no_queue_family || score > selected_score) {
                selected = index;
                selected_score = score;
            }
        }
        return selected == k_no_queue_family ? std::nullopt : std::optional{selected};
    }();
    if (!graphics_queue.has_value()) {
        return std::nullopt;
    }

    const auto compute_queue = select_compute_queue(families, *graphics_queue);
    return QueueSelection{
        .graphics_queue = *graphics_queue,
        .compute_queue = compute_queue,
        .transfer_queue = select_transfer_queue(families, *graphics_queue, compute_queue),
    };
}

[[nodiscard]] constexpr std::uint32_t device_type_priority(VkPhysicalDeviceType device_type) noexcept {
    switch (device_type) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return 4U;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return 3U;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return 2U;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            return 1U;
        default:
            return 0U;
    }
}

struct PhysicalDeviceSelection {
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VulkanPhysicalDeviceInfo info;
    QueueSelection queues;
    std::uint64_t priority{0U};
};

[[nodiscard]] bool supports_swapchain_extension(PFN_vkEnumerateDeviceExtensionProperties enumerate_device_extensions,
                                                VkPhysicalDevice physical_device) {
    std::uint32_t extension_count{0U};
    if (enumerate_device_extensions == nullptr ||
        enumerate_device_extensions(physical_device, nullptr, &extension_count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(extension_count);
    if (enumerate_device_extensions(physical_device, nullptr, &extension_count, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    return std::ranges::any_of(extensions, [](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
    });
}

[[nodiscard]] std::optional<PhysicalDeviceSelection> select_physical_device(
    const std::vector<VkPhysicalDevice>& physical_devices,
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queue_families,
    PFN_vkGetPhysicalDeviceProperties get_physical_device_properties, VkSurfaceKHR surface,
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR get_surface_support,
    PFN_vkEnumerateDeviceExtensionProperties enumerate_device_extensions) {
    std::optional<PhysicalDeviceSelection> selected_device;
    for (const VkPhysicalDevice physical_device : physical_devices) {
        const auto queues = select_queues(get_queue_families, physical_device, surface, get_surface_support);
        if (!queues.has_value()) {
            continue;
        }
        if (surface != VK_NULL_HANDLE && !supports_swapchain_extension(enumerate_device_extensions, physical_device)) {
            continue;
        }

        VkPhysicalDeviceProperties properties{};
        get_physical_device_properties(physical_device, &properties);
        const std::uint64_t priority{(static_cast<std::uint64_t>(device_type_priority(properties.deviceType)) << 32U) |
                                     static_cast<std::uint64_t>(properties.apiVersion)};
        if (!selected_device.has_value() || priority > selected_device->priority) {
            selected_device = {
                .physical_device = physical_device,
                .info = make_physical_device_info(properties),
                .queues = *queues,
                .priority = priority,
            };
        }
    }
    return selected_device;
}

[[nodiscard]] std::vector<std::uint32_t> collect_unique_queue_families(const QueueSelection& queues) {
    std::vector<std::uint32_t> families;
    families.reserve(3U);
    const auto append_unique = [&families](std::uint32_t family_index) {
        if (std::ranges::find(families, family_index) == families.end()) {
            families.push_back(family_index);
        }
    };
    append_unique(queues.graphics_queue);
    if (queues.compute_queue.has_value()) {
        append_unique(*queues.compute_queue);
    }
    if (queues.transfer_queue.has_value()) {
        append_unique(*queues.transfer_queue);
    }
    return families;
}

}  // namespace

struct VulkanInstance::State {
    VulkanLoader loader;
    VkInstance instance{VK_NULL_HANDLE};
    PFN_vkDestroyInstance destroy_instance{nullptr};
    bool surface_extensions_enabled{false};
    std::vector<VkPhysicalDevice> physical_devices;
    VulkanCapabilities capabilities;

    ~State() {
        if (instance != VK_NULL_HANDLE && destroy_instance != nullptr) {
            destroy_instance(instance, nullptr);
        }
    }
};

struct VulkanDevice::State {
    std::shared_ptr<VulkanInstance::State> instance_state;
    VkPhysicalDevice physical_device{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue graphics_queue{VK_NULL_HANDLE};
    VkQueue compute_queue{VK_NULL_HANDLE};
    VkQueue transfer_queue{VK_NULL_HANDLE};
    PFN_vkDestroyDevice destroy_device{nullptr};
    PFN_vkGetDeviceProcAddr get_device_proc_addr{nullptr};
    VulkanDeviceCapabilities capabilities;

    ~State() {
        if (device != VK_NULL_HANDLE && destroy_device != nullptr) {
            destroy_device(device, nullptr);
        }
    }
};

struct VulkanCommandContext::State {
    std::shared_ptr<VulkanDevice::State> device_state;
    VkCommandPool command_pool{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> command_buffers;
    PFN_vkDestroyCommandPool destroy_command_pool{nullptr};
    PFN_vkFreeCommandBuffers free_command_buffers{nullptr};
    PFN_vkDeviceWaitIdle device_wait_idle{nullptr};
    PFN_vkResetCommandBuffer reset_command_buffer{nullptr};
    PFN_vkBeginCommandBuffer begin_command_buffer{nullptr};
    PFN_vkEndCommandBuffer end_command_buffer{nullptr};
    PFN_vkCmdPipelineBarrier cmd_pipeline_barrier{nullptr};
    PFN_vkCmdClearColorImage cmd_clear_color_image{nullptr};
    PFN_vkCmdBeginRenderPass cmd_begin_render_pass{nullptr};
    PFN_vkCmdEndRenderPass cmd_end_render_pass{nullptr};
    PFN_vkCmdBindPipeline cmd_bind_pipeline{nullptr};
    PFN_vkCmdSetViewport cmd_set_viewport{nullptr};
    PFN_vkCmdSetScissor cmd_set_scissor{nullptr};
    PFN_vkCmdDraw cmd_draw{nullptr};
    bool has_submitted_work{false};
    VulkanCommandCapabilities capabilities;

    ~State() {
        if (device_state == nullptr || device_state->device == VK_NULL_HANDLE) {
            return;
        }
        if (has_submitted_work && device_wait_idle != nullptr) {
            device_wait_idle(device_state->device);
        }
        if (command_pool != VK_NULL_HANDLE && free_command_buffers != nullptr && !command_buffers.empty()) {
            free_command_buffers(device_state->device, command_pool, static_cast<std::uint32_t>(command_buffers.size()),
                                 command_buffers.data());
        }
        if (command_pool != VK_NULL_HANDLE && destroy_command_pool != nullptr) {
            destroy_command_pool(device_state->device, command_pool, nullptr);
        }
    }
};

struct VulkanFrameSync::State {
    std::shared_ptr<VulkanDevice::State> device_state;
    std::vector<VkSemaphore> image_available_semaphores;
    std::vector<VkSemaphore> render_finished_semaphores;
    std::vector<VkFence> in_flight_fences;
    PFN_vkDestroySemaphore destroy_semaphore{nullptr};
    PFN_vkDestroyFence destroy_fence{nullptr};
    PFN_vkDeviceWaitIdle device_wait_idle{nullptr};
    PFN_vkWaitForFences wait_for_fences{nullptr};
    PFN_vkResetFences reset_fences{nullptr};
    bool has_submitted_work{false};
    VulkanFrameSyncCapabilities capabilities;

    ~State() {
        if (device_state == nullptr || device_state->device == VK_NULL_HANDLE) {
            return;
        }
        if (has_submitted_work && device_wait_idle != nullptr) {
            device_wait_idle(device_state->device);
        }
        if (destroy_fence != nullptr) {
            for (const VkFence fence : in_flight_fences) {
                destroy_fence(device_state->device, fence, nullptr);
            }
        }
        if (destroy_semaphore != nullptr) {
            for (const VkSemaphore semaphore : render_finished_semaphores) {
                destroy_semaphore(device_state->device, semaphore, nullptr);
            }
            for (const VkSemaphore semaphore : image_available_semaphores) {
                destroy_semaphore(device_state->device, semaphore, nullptr);
            }
        }
    }
};

struct VulkanSurface::State {
    std::shared_ptr<VulkanInstance::State> instance_state;
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    PFN_vkDestroySurfaceKHR destroy_surface{nullptr};
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR get_surface_support{nullptr};
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR get_surface_capabilities{nullptr};
#ifdef _WIN32
    HiddenWindow window;
#endif
    VulkanSurfaceCapabilities capabilities;

    ~State() {
        if (surface != VK_NULL_HANDLE && destroy_surface != nullptr && instance_state != nullptr &&
            instance_state->instance != VK_NULL_HANDLE) {
            destroy_surface(instance_state->instance, surface, nullptr);
        }
    }
};

struct VulkanSwapchain::State {
    std::shared_ptr<VulkanDevice::State> device_state;
    std::shared_ptr<VulkanSurface::State> surface_state;
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    VkFormat image_format{VK_FORMAT_UNDEFINED};
    PFN_vkDestroyImageView destroy_image_view{nullptr};
    PFN_vkDestroySwapchainKHR destroy_swapchain{nullptr};
    PFN_vkDeviceWaitIdle device_wait_idle{nullptr};
    PFN_vkAcquireNextImageKHR acquire_next_image{nullptr};
    PFN_vkQueueSubmit queue_submit{nullptr};
    PFN_vkQueuePresentKHR queue_present{nullptr};
    bool has_submitted_work{false};
    bool recreation_required{false};
    VulkanSwapchainCapabilities capabilities;

    ~State() {
        if (has_submitted_work && device_wait_idle != nullptr && device_state != nullptr &&
            device_state->device != VK_NULL_HANDLE) {
            device_wait_idle(device_state->device);
        }
        if (destroy_image_view != nullptr && device_state != nullptr && device_state->device != VK_NULL_HANDLE) {
            // 图像视图引用交换链镜像，必须在交换链销毁前释放。
            for (const VkImageView image_view : image_views) {
                destroy_image_view(device_state->device, image_view, nullptr);
            }
        }
        if (swapchain != VK_NULL_HANDLE && destroy_swapchain != nullptr && device_state != nullptr &&
            device_state->device != VK_NULL_HANDLE) {
            destroy_swapchain(device_state->device, swapchain, nullptr);
        }
    }
};

struct VulkanTrianglePipeline::State {
    std::shared_ptr<VulkanDevice::State> device_state;
    std::shared_ptr<VulkanSwapchain::State> swapchain_state;
    VkRenderPass render_pass{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    PFN_vkDestroyFramebuffer destroy_framebuffer{nullptr};
    PFN_vkDestroyPipeline destroy_pipeline{nullptr};
    PFN_vkDestroyPipelineLayout destroy_pipeline_layout{nullptr};
    PFN_vkDestroyRenderPass destroy_render_pass{nullptr};
    PFN_vkDeviceWaitIdle device_wait_idle{nullptr};
    bool has_submitted_work{false};
    VulkanTrianglePipelineCapabilities capabilities;

    ~State() {
        if (device_state == nullptr || device_state->device == VK_NULL_HANDLE) {
            return;
        }
        // 管线可能仍被上一帧命令引用；销毁前等待设备空闲可保持 RAII 析构顺序安全。
        if (has_submitted_work && device_wait_idle != nullptr) {
            device_wait_idle(device_state->device);
        }
        if (destroy_framebuffer != nullptr) {
            for (const VkFramebuffer framebuffer : framebuffers) {
                destroy_framebuffer(device_state->device, framebuffer, nullptr);
            }
        }
        if (pipeline != VK_NULL_HANDLE && destroy_pipeline != nullptr) {
            destroy_pipeline(device_state->device, pipeline, nullptr);
        }
        if (pipeline_layout != VK_NULL_HANDLE && destroy_pipeline_layout != nullptr) {
            destroy_pipeline_layout(device_state->device, pipeline_layout, nullptr);
        }
        if (render_pass != VK_NULL_HANDLE && destroy_render_pass != nullptr) {
            destroy_render_pass(device_state->device, render_pass, nullptr);
        }
    }
};

VulkanInstance::VulkanInstance(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

VulkanInstance::VulkanInstance(VulkanInstance&& other) noexcept : state_(std::move(other.state_)) {}

VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

VulkanInstance::~VulkanInstance() = default;

std::optional<VulkanInstance> VulkanInstance::try_create() {
    auto state = std::make_shared<State>();
    if (!state->loader) {
        return std::nullopt;
    }

    const PFN_vkGetInstanceProcAddr get_instance_proc_addr = state->loader.get_instance_proc_addr();
    const auto create_instance =
        load_instance_function<PFN_vkCreateInstance>(get_instance_proc_addr, VK_NULL_HANDLE, "vkCreateInstance");
    if (create_instance == nullptr) {
        return std::nullopt;
    }

    std::uint32_t loader_api_version{VK_API_VERSION_1_0};
    const auto enumerate_instance_version = load_instance_function<PFN_vkEnumerateInstanceVersion>(
        get_instance_proc_addr, VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
    if (enumerate_instance_version != nullptr && enumerate_instance_version(&loader_api_version) != VK_SUCCESS) {
        loader_api_version = VK_API_VERSION_1_0;
    }

    std::vector<const char*> enabled_extensions;
#ifdef _WIN32
    const auto enumerate_instance_extensions = load_instance_function<PFN_vkEnumerateInstanceExtensionProperties>(
        get_instance_proc_addr, VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (enumerate_instance_extensions != nullptr) {
        std::uint32_t extension_count{0U};
        if (enumerate_instance_extensions(nullptr, &extension_count, nullptr) == VK_SUCCESS) {
            std::vector<VkExtensionProperties> extensions(extension_count);
            if (enumerate_instance_extensions(nullptr, &extension_count, extensions.data()) == VK_SUCCESS) {
                const auto has_extension = [&extensions](const char* name) {
                    return std::ranges::any_of(extensions, [name](const VkExtensionProperties& extension) {
                        return std::strcmp(extension.extensionName, name) == 0;
                    });
                };
                if (has_extension(VK_KHR_SURFACE_EXTENSION_NAME) &&
                    has_extension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
                    enabled_extensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
                    state->surface_extensions_enabled = true;
                }
            }
        }
    }
#endif

    const VkApplicationInfo application_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "GisEngine Vulkan Probe",
        .applicationVersion = 1U,
        .pEngineName = "GisEngine",
        .engineVersion = 1U,
        .apiVersion = VK_API_VERSION_1_0,
    };
    const VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .pApplicationInfo = &application_info,
        .enabledLayerCount = 0U,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<std::uint32_t>(enabled_extensions.size()),
        .ppEnabledExtensionNames = enabled_extensions.empty() ? nullptr : enabled_extensions.data(),
    };
    if (create_instance(&create_info, nullptr, &state->instance) != VK_SUCCESS) {
        return std::nullopt;
    }

    state->destroy_instance =
        load_instance_function<PFN_vkDestroyInstance>(get_instance_proc_addr, state->instance, "vkDestroyInstance");
    const auto enumerate_physical_devices = load_instance_function<PFN_vkEnumeratePhysicalDevices>(
        get_instance_proc_addr, state->instance, "vkEnumeratePhysicalDevices");
    const auto get_physical_device_properties = load_instance_function<PFN_vkGetPhysicalDeviceProperties>(
        get_instance_proc_addr, state->instance, "vkGetPhysicalDeviceProperties");
    if (state->destroy_instance == nullptr || enumerate_physical_devices == nullptr ||
        get_physical_device_properties == nullptr) {
        return std::nullopt;
    }

    std::uint32_t device_count{0U};
    if (enumerate_physical_devices(state->instance, &device_count, nullptr) != VK_SUCCESS || device_count == 0U) {
        return std::nullopt;
    }
    state->physical_devices.resize(device_count);
    const VkResult enumerate_result =
        enumerate_physical_devices(state->instance, &device_count, state->physical_devices.data());
    if (enumerate_result != VK_SUCCESS && enumerate_result != VK_INCOMPLETE) {
        return std::nullopt;
    }
    state->physical_devices.resize(device_count);
    if (state->physical_devices.empty()) {
        return std::nullopt;
    }

    state->capabilities.available = true;
    state->capabilities.instance_api_version = loader_api_version;
    state->capabilities.physical_devices.reserve(state->physical_devices.size());
    for (const VkPhysicalDevice physical_device : state->physical_devices) {
        VkPhysicalDeviceProperties properties{};
        get_physical_device_properties(physical_device, &properties);
        state->capabilities.physical_devices.push_back(make_physical_device_info(properties));
    }
    return VulkanInstance{std::move(state)};
}

const VulkanCapabilities& VulkanInstance::capabilities() const noexcept {
    static const VulkanCapabilities unavailable{};
    return state_ != nullptr ? state_->capabilities : unavailable;
}

VulkanDevice::VulkanDevice(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

VulkanDevice::VulkanDevice(VulkanDevice&& other) noexcept : state_(std::move(other.state_)) {}

VulkanDevice& VulkanDevice::operator=(VulkanDevice&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

VulkanDevice::~VulkanDevice() = default;

std::optional<VulkanDevice> VulkanDevice::try_create(const VulkanInstance& instance) {
    return try_create_impl(instance, nullptr);
}

std::optional<VulkanDevice> VulkanDevice::try_create(const VulkanInstance& instance, const VulkanSurface& surface) {
    return try_create_impl(instance, &surface);
}

bool VulkanDevice::can_use_surface(const VulkanInstance& instance, const VulkanSurface* surface) noexcept {
    return surface == nullptr ||
           (surface->state_ != nullptr && surface->state_->instance_state.get() == instance.state_.get() &&
            surface->state_->surface != VK_NULL_HANDLE);
}

std::optional<VulkanDevice> VulkanDevice::try_create_impl(const VulkanInstance& instance,
                                                          const VulkanSurface* surface) {
    if (instance.state_ == nullptr || !instance.state_->capabilities.available) {
        return std::nullopt;
    }

    if (!can_use_surface(instance, surface)) {
        return std::nullopt;
    }

    const PFN_vkGetInstanceProcAddr get_instance_proc_addr = instance.state_->loader.get_instance_proc_addr();
    const auto get_queue_families = load_instance_function<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        get_instance_proc_addr, instance.state_->instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    const auto get_physical_device_properties = load_instance_function<PFN_vkGetPhysicalDeviceProperties>(
        get_instance_proc_addr, instance.state_->instance, "vkGetPhysicalDeviceProperties");
    const auto create_device =
        load_instance_function<PFN_vkCreateDevice>(get_instance_proc_addr, instance.state_->instance, "vkCreateDevice");
    const auto get_device_proc_addr = load_instance_function<PFN_vkGetDeviceProcAddr>(
        get_instance_proc_addr, instance.state_->instance, "vkGetDeviceProcAddr");
    const auto get_surface_support =
        surface == nullptr
            ? nullptr
            : load_instance_function<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
                  get_instance_proc_addr, instance.state_->instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    const auto enumerate_device_extensions =
        surface == nullptr
            ? nullptr
            : load_instance_function<PFN_vkEnumerateDeviceExtensionProperties>(
                  get_instance_proc_addr, instance.state_->instance, "vkEnumerateDeviceExtensionProperties");
    if (get_queue_families == nullptr || get_physical_device_properties == nullptr || create_device == nullptr ||
        get_device_proc_addr == nullptr ||
        (surface != nullptr && (get_surface_support == nullptr || enumerate_device_extensions == nullptr))) {
        return std::nullopt;
    }

    const VkSurfaceKHR native_surface = surface != nullptr ? surface->state_->surface : VK_NULL_HANDLE;
    auto selected_device =
        select_physical_device(instance.state_->physical_devices, get_queue_families, get_physical_device_properties,
                               native_surface, get_surface_support, enumerate_device_extensions);
    if (!selected_device.has_value()) {
        return std::nullopt;
    }

    const std::vector<const char*> enabled_device_extensions =
        surface != nullptr ? std::vector<const char*>{VK_KHR_SWAPCHAIN_EXTENSION_NAME} : std::vector<const char*>{};

    const std::vector<std::uint32_t> queue_families = collect_unique_queue_families(selected_device->queues);
    constexpr float queue_priority{1.0F};
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    queue_create_infos.reserve(queue_families.size());
    for (const std::uint32_t queue_family : queue_families) {
        queue_create_infos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .queueFamilyIndex = queue_family,
            .queueCount = 1U,
            .pQueuePriorities = &queue_priority,
        });
    }
    const VkDeviceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .queueCreateInfoCount = static_cast<std::uint32_t>(queue_create_infos.size()),
        .pQueueCreateInfos = queue_create_infos.data(),
        .enabledLayerCount = 0U,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<std::uint32_t>(enabled_device_extensions.size()),
        .ppEnabledExtensionNames = enabled_device_extensions.empty() ? nullptr : enabled_device_extensions.data(),
        .pEnabledFeatures = nullptr,
    };

    VkDevice device{VK_NULL_HANDLE};
    if (create_device(selected_device->physical_device, &create_info, nullptr, &device) != VK_SUCCESS) {
        return std::nullopt;
    }

    const auto destroy_device =
        load_device_function<PFN_vkDestroyDevice>(get_device_proc_addr, device, "vkDestroyDevice");
    const auto get_device_queue =
        load_device_function<PFN_vkGetDeviceQueue>(get_device_proc_addr, device, "vkGetDeviceQueue");
    if (destroy_device == nullptr || get_device_queue == nullptr) {
        if (destroy_device != nullptr) {
            destroy_device(device, nullptr);
        }
        return std::nullopt;
    }

    auto state = std::make_shared<State>();
    state->instance_state = instance.state_;
    state->physical_device = selected_device->physical_device;
    state->device = device;
    state->destroy_device = destroy_device;
    state->get_device_proc_addr = get_device_proc_addr;
    get_device_queue(device, selected_device->queues.graphics_queue, 0U, &state->graphics_queue);
    if (selected_device->queues.compute_queue.has_value()) {
        get_device_queue(device, *selected_device->queues.compute_queue, 0U, &state->compute_queue);
    }
    if (selected_device->queues.transfer_queue.has_value()) {
        get_device_queue(device, *selected_device->queues.transfer_queue, 0U, &state->transfer_queue);
    }
    if (state->graphics_queue == VK_NULL_HANDLE ||
        (selected_device->queues.compute_queue.has_value() && state->compute_queue == VK_NULL_HANDLE) ||
        (selected_device->queues.transfer_queue.has_value() && state->transfer_queue == VK_NULL_HANDLE)) {
        return std::nullopt;
    }

    state->capabilities = {
        .available = true,
        .physical_device = std::move(selected_device->info),
        .graphics_queue = {.family_index = selected_device->queues.graphics_queue, .available = true},
        .compute_queue =
            {
                .family_index = selected_device->queues.compute_queue.value_or(0U),
                .available = selected_device->queues.compute_queue.has_value(),
            },
        .transfer_queue =
            {
                .family_index = selected_device->queues.transfer_queue.value_or(0U),
                .available = selected_device->queues.transfer_queue.has_value(),
            },
    };
    return VulkanDevice{std::move(state)};
}

const VulkanDeviceCapabilities& VulkanDevice::capabilities() const noexcept {
    static const VulkanDeviceCapabilities unavailable{};
    return state_ != nullptr ? state_->capabilities : unavailable;
}

VulkanCommandContext::VulkanCommandContext(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

VulkanCommandContext::VulkanCommandContext(VulkanCommandContext&& other) noexcept : state_(std::move(other.state_)) {}

VulkanCommandContext& VulkanCommandContext::operator=(VulkanCommandContext&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

VulkanCommandContext::~VulkanCommandContext() = default;

std::optional<VulkanCommandContext> VulkanCommandContext::try_create(const VulkanDevice& device,
                                                                     std::uint32_t command_buffer_count) {
    if (device.state_ == nullptr || device.state_->device == VK_NULL_HANDLE || command_buffer_count == 0U ||
        device.state_->get_device_proc_addr == nullptr) {
        return std::nullopt;
    }

    const auto create_command_pool = load_device_function<PFN_vkCreateCommandPool>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCreateCommandPool");
    const auto destroy_command_pool = load_device_function<PFN_vkDestroyCommandPool>(
        device.state_->get_device_proc_addr, device.state_->device, "vkDestroyCommandPool");
    const auto allocate_command_buffers = load_device_function<PFN_vkAllocateCommandBuffers>(
        device.state_->get_device_proc_addr, device.state_->device, "vkAllocateCommandBuffers");
    const auto free_command_buffers = load_device_function<PFN_vkFreeCommandBuffers>(
        device.state_->get_device_proc_addr, device.state_->device, "vkFreeCommandBuffers");
    const auto device_wait_idle = load_device_function<PFN_vkDeviceWaitIdle>(device.state_->get_device_proc_addr,
                                                                             device.state_->device, "vkDeviceWaitIdle");
    const auto reset_command_buffer = load_device_function<PFN_vkResetCommandBuffer>(
        device.state_->get_device_proc_addr, device.state_->device, "vkResetCommandBuffer");
    const auto begin_command_buffer = load_device_function<PFN_vkBeginCommandBuffer>(
        device.state_->get_device_proc_addr, device.state_->device, "vkBeginCommandBuffer");
    const auto end_command_buffer = load_device_function<PFN_vkEndCommandBuffer>(
        device.state_->get_device_proc_addr, device.state_->device, "vkEndCommandBuffer");
    const auto cmd_pipeline_barrier = load_device_function<PFN_vkCmdPipelineBarrier>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCmdPipelineBarrier");
    const auto cmd_clear_color_image = load_device_function<PFN_vkCmdClearColorImage>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCmdClearColorImage");
    const auto cmd_begin_render_pass = load_device_function<PFN_vkCmdBeginRenderPass>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCmdBeginRenderPass");
    const auto cmd_end_render_pass = load_device_function<PFN_vkCmdEndRenderPass>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCmdEndRenderPass");
    const auto cmd_bind_pipeline = load_device_function<PFN_vkCmdBindPipeline>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCmdBindPipeline");
    const auto cmd_set_viewport = load_device_function<PFN_vkCmdSetViewport>(device.state_->get_device_proc_addr,
                                                                             device.state_->device, "vkCmdSetViewport");
    const auto cmd_set_scissor = load_device_function<PFN_vkCmdSetScissor>(device.state_->get_device_proc_addr,
                                                                           device.state_->device, "vkCmdSetScissor");
    const auto cmd_draw =
        load_device_function<PFN_vkCmdDraw>(device.state_->get_device_proc_addr, device.state_->device, "vkCmdDraw");
    if (create_command_pool == nullptr || destroy_command_pool == nullptr || allocate_command_buffers == nullptr ||
        free_command_buffers == nullptr || device_wait_idle == nullptr || reset_command_buffer == nullptr ||
        begin_command_buffer == nullptr || end_command_buffer == nullptr || cmd_pipeline_barrier == nullptr ||
        cmd_clear_color_image == nullptr || cmd_begin_render_pass == nullptr || cmd_end_render_pass == nullptr ||
        cmd_bind_pipeline == nullptr || cmd_set_viewport == nullptr || cmd_set_scissor == nullptr ||
        cmd_draw == nullptr) {
        return std::nullopt;
    }

    auto state = std::make_unique<State>();
    state->device_state = device.state_;
    state->destroy_command_pool = destroy_command_pool;
    state->free_command_buffers = free_command_buffers;
    state->device_wait_idle = device_wait_idle;
    state->reset_command_buffer = reset_command_buffer;
    state->begin_command_buffer = begin_command_buffer;
    state->end_command_buffer = end_command_buffer;
    state->cmd_pipeline_barrier = cmd_pipeline_barrier;
    state->cmd_clear_color_image = cmd_clear_color_image;
    state->cmd_begin_render_pass = cmd_begin_render_pass;
    state->cmd_end_render_pass = cmd_end_render_pass;
    state->cmd_bind_pipeline = cmd_bind_pipeline;
    state->cmd_set_viewport = cmd_set_viewport;
    state->cmd_set_scissor = cmd_set_scissor;
    state->cmd_draw = cmd_draw;
    const VkCommandPoolCreateInfo pool_create_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = device.state_->capabilities.graphics_queue.family_index,
    };
    if (create_command_pool(device.state_->device, &pool_create_info, nullptr, &state->command_pool) != VK_SUCCESS) {
        return std::nullopt;
    }

    state->command_buffers.resize(command_buffer_count);
    const VkCommandBufferAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = state->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = command_buffer_count,
    };
    if (allocate_command_buffers(device.state_->device, &allocate_info, state->command_buffers.data()) != VK_SUCCESS) {
        return std::nullopt;
    }
    state->capabilities = {
        .available = true,
        .queue_family_index = device.state_->capabilities.graphics_queue.family_index,
        .command_buffer_count = command_buffer_count,
    };
    return VulkanCommandContext{std::move(state)};
}

const VulkanCommandCapabilities& VulkanCommandContext::capabilities() const noexcept {
    static const VulkanCommandCapabilities unavailable{};
    return state_ != nullptr ? state_->capabilities : unavailable;
}

VulkanFrameSync::VulkanFrameSync(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

VulkanFrameSync::VulkanFrameSync(VulkanFrameSync&& other) noexcept : state_(std::move(other.state_)) {}

VulkanFrameSync& VulkanFrameSync::operator=(VulkanFrameSync&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

VulkanFrameSync::~VulkanFrameSync() = default;

std::optional<VulkanFrameSync> VulkanFrameSync::try_create(const VulkanDevice& device, std::uint32_t frame_count) {
    if (device.state_ == nullptr || device.state_->device == VK_NULL_HANDLE || frame_count == 0U ||
        device.state_->get_device_proc_addr == nullptr) {
        return std::nullopt;
    }

    const auto create_semaphore = load_device_function<PFN_vkCreateSemaphore>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCreateSemaphore");
    const auto destroy_semaphore = load_device_function<PFN_vkDestroySemaphore>(
        device.state_->get_device_proc_addr, device.state_->device, "vkDestroySemaphore");
    const auto create_fence = load_device_function<PFN_vkCreateFence>(device.state_->get_device_proc_addr,
                                                                      device.state_->device, "vkCreateFence");
    const auto destroy_fence = load_device_function<PFN_vkDestroyFence>(device.state_->get_device_proc_addr,
                                                                        device.state_->device, "vkDestroyFence");
    const auto device_wait_idle = load_device_function<PFN_vkDeviceWaitIdle>(device.state_->get_device_proc_addr,
                                                                             device.state_->device, "vkDeviceWaitIdle");
    const auto wait_for_fences = load_device_function<PFN_vkWaitForFences>(device.state_->get_device_proc_addr,
                                                                           device.state_->device, "vkWaitForFences");
    const auto reset_fences = load_device_function<PFN_vkResetFences>(device.state_->get_device_proc_addr,
                                                                      device.state_->device, "vkResetFences");
    if (create_semaphore == nullptr || destroy_semaphore == nullptr || create_fence == nullptr ||
        destroy_fence == nullptr || device_wait_idle == nullptr || wait_for_fences == nullptr ||
        reset_fences == nullptr) {
        return std::nullopt;
    }

    auto state = std::make_unique<State>();
    state->device_state = device.state_;
    state->destroy_semaphore = destroy_semaphore;
    state->destroy_fence = destroy_fence;
    state->device_wait_idle = device_wait_idle;
    state->wait_for_fences = wait_for_fences;
    state->reset_fences = reset_fences;
    state->image_available_semaphores.reserve(frame_count);
    state->render_finished_semaphores.reserve(frame_count);
    state->in_flight_fences.reserve(frame_count);
    const VkSemaphoreCreateInfo semaphore_create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
    };
    const VkFenceCreateInfo fence_create_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    for (std::uint32_t index{0U}; index < frame_count; ++index) {
        VkSemaphore image_available{VK_NULL_HANDLE};
        VkSemaphore render_finished{VK_NULL_HANDLE};
        VkFence in_flight{VK_NULL_HANDLE};
        if (create_semaphore(device.state_->device, &semaphore_create_info, nullptr, &image_available) != VK_SUCCESS ||
            create_semaphore(device.state_->device, &semaphore_create_info, nullptr, &render_finished) != VK_SUCCESS ||
            create_fence(device.state_->device, &fence_create_info, nullptr, &in_flight) != VK_SUCCESS) {
            if (in_flight != VK_NULL_HANDLE) {
                destroy_fence(device.state_->device, in_flight, nullptr);
            }
            if (render_finished != VK_NULL_HANDLE) {
                destroy_semaphore(device.state_->device, render_finished, nullptr);
            }
            if (image_available != VK_NULL_HANDLE) {
                destroy_semaphore(device.state_->device, image_available, nullptr);
            }
            return std::nullopt;
        }
        state->image_available_semaphores.push_back(image_available);
        state->render_finished_semaphores.push_back(render_finished);
        state->in_flight_fences.push_back(in_flight);
    }
    state->capabilities = {.available = true, .frame_count = frame_count};
    return VulkanFrameSync{std::move(state)};
}

const VulkanFrameSyncCapabilities& VulkanFrameSync::capabilities() const noexcept {
    static const VulkanFrameSyncCapabilities unavailable{};
    return state_ != nullptr ? state_->capabilities : unavailable;
}

VulkanSurface::VulkanSurface(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

VulkanSurface::VulkanSurface(VulkanSurface&& other) noexcept : state_(std::move(other.state_)) {}

VulkanSurface& VulkanSurface::operator=(VulkanSurface&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

VulkanSurface::~VulkanSurface() = default;

std::optional<VulkanSurface> VulkanSurface::try_create(const VulkanInstance& instance) {
    return try_create(instance, VulkanSurfaceCreateInfo{});
}

std::optional<VulkanSurface> VulkanSurface::try_create(const VulkanInstance& instance,
                                                       const VulkanSurfaceCreateInfo& surface_create_info) {
#ifdef _WIN32
    if (instance.state_ == nullptr || !instance.state_->capabilities.available ||
        !instance.state_->surface_extensions_enabled || instance.state_->physical_devices.empty()) {
        return std::nullopt;
    }

    if (surface_create_info.width == 0U || surface_create_info.height == 0U) {
        return std::nullopt;
    }
    auto window =
        HiddenWindow::try_create(surface_create_info.width, surface_create_info.height, surface_create_info.visible);
    if (!window.has_value()) {
        return std::nullopt;
    }

    const PFN_vkGetInstanceProcAddr get_instance_proc_addr = instance.state_->loader.get_instance_proc_addr();
    const auto create_surface = load_instance_function<PFN_vkCreateWin32SurfaceKHR>(
        get_instance_proc_addr, instance.state_->instance, "vkCreateWin32SurfaceKHR");
    const auto destroy_surface = load_instance_function<PFN_vkDestroySurfaceKHR>(
        get_instance_proc_addr, instance.state_->instance, "vkDestroySurfaceKHR");
    const auto get_surface_support = load_instance_function<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
        get_instance_proc_addr, instance.state_->instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    const auto get_surface_capabilities = load_instance_function<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
        get_instance_proc_addr, instance.state_->instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    if (create_surface == nullptr || destroy_surface == nullptr || get_surface_support == nullptr ||
        get_surface_capabilities == nullptr) {
        return std::nullopt;
    }

    const VkWin32SurfaceCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0U,
        .hinstance = window->instance(),
        .hwnd = window->window(),
    };
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    if (create_surface(instance.state_->instance, &create_info, nullptr, &surface) != VK_SUCCESS) {
        return std::nullopt;
    }

    // Vulkan 查询函数会完整写入结构体，调用前清零是官方 API 约定。
    VkSurfaceCapabilitiesKHR native_capabilities{};  // NOLINT(bugprone-invalid-enum-default-initialization)
    const VkResult capabilities_result =
        get_surface_capabilities(instance.state_->physical_devices.front(), surface, &native_capabilities);
    if (capabilities_result != VK_SUCCESS) {
        destroy_surface(instance.state_->instance, surface, nullptr);
        return std::nullopt;
    }

    auto state = std::make_shared<State>();
    state->instance_state = instance.state_;
    state->surface = surface;
    state->destroy_surface = destroy_surface;
    state->get_surface_support = get_surface_support;
    state->get_surface_capabilities = get_surface_capabilities;
    state->window = std::move(*window);
    const WindowExtent window_extent = state->window.client_extent();
    const bool extent_is_variable =
        native_capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max() ||
        native_capabilities.currentExtent.height == std::numeric_limits<std::uint32_t>::max();
    state->capabilities = {
        .available = true,
        .width = extent_is_variable ? std::clamp(window_extent.width, native_capabilities.minImageExtent.width,
                                                 native_capabilities.maxImageExtent.width)
                                    : native_capabilities.currentExtent.width,
        .height = extent_is_variable ? std::clamp(window_extent.height, native_capabilities.minImageExtent.height,
                                                  native_capabilities.maxImageExtent.height)
                                     : native_capabilities.currentExtent.height,
    };
    return VulkanSurface{std::move(state)};
#else
    static_cast<void>(instance);
    return std::nullopt;
#endif
}

const VulkanSurfaceCapabilities& VulkanSurface::capabilities() const noexcept {
    static const VulkanSurfaceCapabilities unavailable{};
    return state_ != nullptr ? state_->capabilities : unavailable;
}

bool VulkanSurface::process_events() const noexcept {
#ifdef _WIN32
    if (state_ == nullptr || !state_->window.process_events()) {
        return false;
    }
    const WindowExtent extent = state_->window.client_extent();
    if (extent.width > 0U && extent.height > 0U) {
        state_->capabilities.width = extent.width;
        state_->capabilities.height = extent.height;
    }
    return true;
#else
    return false;
#endif
}

bool VulkanSurface::has_pending_resize() const noexcept {
#ifdef _WIN32
    return state_ != nullptr && state_->window.has_pending_resize();
#else
    return false;
#endif
}

bool VulkanSurface::has_renderable_extent() const noexcept {
#ifdef _WIN32
    return state_ != nullptr && state_->window.has_renderable_extent();
#else
    return false;
#endif
}

void VulkanSurface::acknowledge_resize() noexcept {
#ifdef _WIN32
    if (state_ != nullptr) {
        state_->window.acknowledge_resize();
    }
#endif
}

bool VulkanSurface::supports_present(const VulkanDevice& device) const noexcept {
    if (state_ == nullptr || device.state_ == nullptr || state_->get_surface_support == nullptr ||
        device.state_->physical_device == VK_NULL_HANDLE ||
        device.state_->instance_state.get() != state_->instance_state.get()) {
        return false;
    }
    return queue_supports_present(state_->get_surface_support, device.state_->physical_device,
                                  device.state_->capabilities.graphics_queue.family_index, state_->surface);
}

VulkanSwapchain::VulkanSwapchain(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}

VulkanSwapchain::VulkanSwapchain(VulkanSwapchain&& other) noexcept : state_(std::move(other.state_)) {}

VulkanSwapchain& VulkanSwapchain::operator=(VulkanSwapchain&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

VulkanSwapchain::~VulkanSwapchain() = default;

namespace {

[[nodiscard]] std::optional<std::vector<VkImage>> get_images(PFN_vkGetSwapchainImagesKHR get_swapchain_images,
                                                             VkDevice device, VkSwapchainKHR swapchain) {
    std::uint32_t image_count{0U};
    if (get_swapchain_images(device, swapchain, &image_count, nullptr) != VK_SUCCESS || image_count == 0U) {
        return std::nullopt;
    }

    std::vector<VkImage> images(image_count);
    if (get_swapchain_images(device, swapchain, &image_count, images.data()) != VK_SUCCESS || image_count == 0U) {
        return std::nullopt;
    }
    images.resize(image_count);
    return images;
}

[[nodiscard]] std::optional<std::vector<VkImageView>> create_image_views(PFN_vkCreateImageView create_image_view,
                                                                         PFN_vkDestroyImageView destroy_image_view,
                                                                         VkDevice device,
                                                                         const std::vector<VkImage>& images,
                                                                         VkFormat format) {
    std::vector<VkImageView> image_views;
    image_views.reserve(images.size());
    for (const VkImage image : images) {
        const VkImageViewCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .components =
                {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY,
                },
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0U,
                    .levelCount = 1U,
                    .baseArrayLayer = 0U,
                    .layerCount = 1U,
                },
        };
        VkImageView image_view{VK_NULL_HANDLE};
        if (create_image_view(device, &create_info, nullptr, &image_view) != VK_SUCCESS) {
            // 已创建的图像视图由本函数回滚，调用方只接收完整结果。
            for (const VkImageView created_image_view : image_views) {
                destroy_image_view(device, created_image_view, nullptr);
            }
            return std::nullopt;
        }
        image_views.push_back(image_view);
    }
    return image_views;
}

[[nodiscard]] std::optional<std::vector<std::uint32_t>> read_spirv_words(const std::string& file_path) {
    if (file_path.empty()) {
        return std::nullopt;
    }

    std::ifstream input{file_path, std::ios::binary | std::ios::ate};
    if (!input) {
        return std::nullopt;
    }
    const std::streamsize byte_count = input.tellg();
    if (byte_count <= 0 || (byte_count % static_cast<std::streamsize>(sizeof(std::uint32_t))) != 0) {
        return std::nullopt;
    }
    const auto word_count = static_cast<std::size_t>(byte_count) / sizeof(std::uint32_t);
    std::vector<std::uint32_t> words(word_count);
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(words.data()), byte_count);
    // SPIR-V 魔数既验证二进制格式，也避免将文本或不完整文件传给 Vulkan 驱动。
    constexpr std::uint32_t k_spirv_magic{0x07230203U};
    if (!input || words.size() < 5U || words.front() != k_spirv_magic) {
        return std::nullopt;
    }
    return words;
}

}  // namespace

// 交换链创建需按 Vulkan 能力协商顺序完成；拆散会掩盖资源回滚与销毁依赖。
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
std::optional<VulkanSwapchain> VulkanSwapchain::try_create(const VulkanDevice& device, const VulkanSurface& surface) {
    if (device.state_ == nullptr || surface.state_ == nullptr ||
        device.state_->instance_state.get() != surface.state_->instance_state.get() ||
        device.state_->device == VK_NULL_HANDLE || surface.state_->surface == VK_NULL_HANDLE ||
        device.state_->get_device_proc_addr == nullptr) {
        return std::nullopt;
    }

    const auto get_instance_proc_addr = device.state_->instance_state->loader.get_instance_proc_addr();
    const auto get_surface_capabilities = load_instance_function<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
        get_instance_proc_addr, device.state_->instance_state->instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    const auto get_surface_formats = load_instance_function<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
        get_instance_proc_addr, device.state_->instance_state->instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    const auto get_present_modes = load_instance_function<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
        get_instance_proc_addr, device.state_->instance_state->instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    const auto create_swapchain = load_device_function<PFN_vkCreateSwapchainKHR>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCreateSwapchainKHR");
    const auto destroy_swapchain = load_device_function<PFN_vkDestroySwapchainKHR>(
        device.state_->get_device_proc_addr, device.state_->device, "vkDestroySwapchainKHR");
    const auto get_swapchain_images = load_device_function<PFN_vkGetSwapchainImagesKHR>(
        device.state_->get_device_proc_addr, device.state_->device, "vkGetSwapchainImagesKHR");
    const auto create_image_view = load_device_function<PFN_vkCreateImageView>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCreateImageView");
    const auto destroy_image_view = load_device_function<PFN_vkDestroyImageView>(
        device.state_->get_device_proc_addr, device.state_->device, "vkDestroyImageView");
    const auto device_wait_idle = load_device_function<PFN_vkDeviceWaitIdle>(device.state_->get_device_proc_addr,
                                                                             device.state_->device, "vkDeviceWaitIdle");
    const auto acquire_next_image = load_device_function<PFN_vkAcquireNextImageKHR>(
        device.state_->get_device_proc_addr, device.state_->device, "vkAcquireNextImageKHR");
    const auto queue_submit = load_device_function<PFN_vkQueueSubmit>(device.state_->get_device_proc_addr,
                                                                      device.state_->device, "vkQueueSubmit");
    const auto queue_present = load_device_function<PFN_vkQueuePresentKHR>(device.state_->get_device_proc_addr,
                                                                           device.state_->device, "vkQueuePresentKHR");
    if (get_surface_capabilities == nullptr || get_surface_formats == nullptr || get_present_modes == nullptr ||
        create_swapchain == nullptr || destroy_swapchain == nullptr || get_swapchain_images == nullptr ||
        create_image_view == nullptr || destroy_image_view == nullptr || device_wait_idle == nullptr ||
        acquire_next_image == nullptr || queue_submit == nullptr || queue_present == nullptr) {
        return std::nullopt;
    }

    VkSurfaceCapabilitiesKHR capabilities{};  // NOLINT(bugprone-invalid-enum-default-initialization)
    if (get_surface_capabilities(device.state_->physical_device, surface.state_->surface, &capabilities) !=
        VK_SUCCESS) {
        return std::nullopt;
    }

    std::uint32_t format_count{0U};
    if (get_surface_formats(device.state_->physical_device, surface.state_->surface, &format_count, nullptr) !=
            VK_SUCCESS ||
        format_count == 0U) {
        return std::nullopt;
    }
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    if (get_surface_formats(device.state_->physical_device, surface.state_->surface, &format_count, formats.data()) !=
        VK_SUCCESS) {
        return std::nullopt;
    }
    formats.resize(format_count);
    const auto format = std::ranges::find_if(formats, [](const VkSurfaceFormatKHR& candidate) {
        return candidate.format == VK_FORMAT_B8G8R8A8_SRGB && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    const VkSurfaceFormatKHR selected_format = format != formats.end() ? *format : formats.front();

    std::uint32_t present_mode_count{0U};
    if (get_present_modes(device.state_->physical_device, surface.state_->surface, &present_mode_count, nullptr) !=
            VK_SUCCESS ||
        present_mode_count == 0U) {
        return std::nullopt;
    }
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    if (get_present_modes(device.state_->physical_device, surface.state_->surface, &present_mode_count,
                          present_modes.data()) != VK_SUCCESS) {
        return std::nullopt;
    }
    present_modes.resize(present_mode_count);
    const auto mailbox = std::ranges::find(present_modes, VK_PRESENT_MODE_MAILBOX_KHR);
    const auto fifo = std::ranges::find(present_modes, VK_PRESENT_MODE_FIFO_KHR);
    if (fifo == present_modes.end()) {
        return std::nullopt;
    }
    const VkPresentModeKHR selected_present_mode =
        mailbox != present_modes.end() ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;

    const bool variable_extent = capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max() ||
                                 capabilities.currentExtent.height == std::numeric_limits<std::uint32_t>::max();
    const VkExtent2D extent{
        .width = variable_extent ? std::clamp(surface.state_->capabilities.width, capabilities.minImageExtent.width,
                                              capabilities.maxImageExtent.width)
                                 : capabilities.currentExtent.width,
        .height = variable_extent ? std::clamp(surface.state_->capabilities.height, capabilities.minImageExtent.height,
                                               capabilities.maxImageExtent.height)
                                  : capabilities.currentExtent.height,
    };
    if (extent.width == 0U || extent.height == 0U) {
        return std::nullopt;
    }
    const std::uint32_t requested_image_count = capabilities.minImageCount + 1U;
    const std::uint32_t image_count = capabilities.maxImageCount == 0U
                                          ? requested_image_count
                                          : std::min(requested_image_count, capabilities.maxImageCount);
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0U) {
        return std::nullopt;
    }
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4U> composite_alpha_candidates{
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    const auto composite_alpha =
        std::ranges::find_if(composite_alpha_candidates, [&capabilities](VkCompositeAlphaFlagBitsKHR candidate) {
            return (capabilities.supportedCompositeAlpha & candidate) != 0U;
        });
    if (composite_alpha == composite_alpha_candidates.end()) {
        return std::nullopt;
    }
    const VkSwapchainCreateInfoKHR create_info{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0U,
        .surface = surface.state_->surface,
        .minImageCount = image_count,
        .imageFormat = selected_format.format,
        .imageColorSpace = selected_format.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1U,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0U,
        .pQueueFamilyIndices = nullptr,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = *composite_alpha,
        .presentMode = selected_present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };
    auto state = std::make_shared<State>();
    state->device_state = device.state_;
    state->surface_state = surface.state_;
    state->destroy_image_view = destroy_image_view;
    state->destroy_swapchain = destroy_swapchain;
    state->device_wait_idle = device_wait_idle;
    state->acquire_next_image = acquire_next_image;
    state->queue_submit = queue_submit;
    state->queue_present = queue_present;
    if (create_swapchain(device.state_->device, &create_info, nullptr, &state->swapchain) != VK_SUCCESS) {
        return std::nullopt;
    }

    auto images = get_images(get_swapchain_images, device.state_->device, state->swapchain);
    if (!images.has_value()) {
        return std::nullopt;
    }
    auto image_views = create_image_views(create_image_view, destroy_image_view, device.state_->device, *images,
                                          selected_format.format);
    if (!image_views.has_value()) {
        return std::nullopt;
    }
    state->images = std::move(*images);
    state->image_views = std::move(*image_views);
    state->image_format = selected_format.format;
    state->capabilities = {
        .available = true,
        .width = extent.width,
        .height = extent.height,
        .image_count = static_cast<std::uint32_t>(state->images.size()),
        .image_view_count = static_cast<std::uint32_t>(state->image_views.size()),
    };
    return VulkanSwapchain{std::move(state)};
}

VulkanTrianglePipeline::VulkanTrianglePipeline(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

VulkanTrianglePipeline::VulkanTrianglePipeline(VulkanTrianglePipeline&& other) noexcept
    : state_(std::move(other.state_)) {}

VulkanTrianglePipeline& VulkanTrianglePipeline::operator=(VulkanTrianglePipeline&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

VulkanTrianglePipeline::~VulkanTrianglePipeline() = default;

std::optional<VulkanTrianglePipeline> VulkanTrianglePipeline::try_create(const VulkanDevice& device,
                                                                         const VulkanSwapchain& swapchain,
                                                                         const VulkanShaderBinaryPaths& shader_paths) {
    if (device.state_ == nullptr || swapchain.state_ == nullptr ||
        swapchain.state_->device_state.get() != device.state_.get() || device.state_->device == VK_NULL_HANDLE ||
        swapchain.state_->image_views.empty() || swapchain.state_->image_format == VK_FORMAT_UNDEFINED ||
        device.state_->get_device_proc_addr == nullptr) {
        return std::nullopt;
    }

    const auto vertex_words = read_spirv_words(shader_paths.vertex_shader_path);
    const auto fragment_words = read_spirv_words(shader_paths.fragment_shader_path);
    if (!vertex_words.has_value() || !fragment_words.has_value()) {
        return std::nullopt;
    }

    const auto create_shader_module = load_device_function<PFN_vkCreateShaderModule>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCreateShaderModule");
    const auto destroy_shader_module = load_device_function<PFN_vkDestroyShaderModule>(
        device.state_->get_device_proc_addr, device.state_->device, "vkDestroyShaderModule");
    const auto create_render_pass = load_device_function<PFN_vkCreateRenderPass>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCreateRenderPass");
    const auto destroy_render_pass = load_device_function<PFN_vkDestroyRenderPass>(
        device.state_->get_device_proc_addr, device.state_->device, "vkDestroyRenderPass");
    const auto create_pipeline_layout = load_device_function<PFN_vkCreatePipelineLayout>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCreatePipelineLayout");
    const auto destroy_pipeline_layout = load_device_function<PFN_vkDestroyPipelineLayout>(
        device.state_->get_device_proc_addr, device.state_->device, "vkDestroyPipelineLayout");
    const auto create_graphics_pipelines = load_device_function<PFN_vkCreateGraphicsPipelines>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCreateGraphicsPipelines");
    const auto destroy_pipeline = load_device_function<PFN_vkDestroyPipeline>(
        device.state_->get_device_proc_addr, device.state_->device, "vkDestroyPipeline");
    const auto create_framebuffer = load_device_function<PFN_vkCreateFramebuffer>(
        device.state_->get_device_proc_addr, device.state_->device, "vkCreateFramebuffer");
    const auto destroy_framebuffer = load_device_function<PFN_vkDestroyFramebuffer>(
        device.state_->get_device_proc_addr, device.state_->device, "vkDestroyFramebuffer");
    const auto device_wait_idle = load_device_function<PFN_vkDeviceWaitIdle>(device.state_->get_device_proc_addr,
                                                                             device.state_->device, "vkDeviceWaitIdle");
    if (create_shader_module == nullptr || destroy_shader_module == nullptr || create_render_pass == nullptr ||
        destroy_render_pass == nullptr || create_pipeline_layout == nullptr || destroy_pipeline_layout == nullptr ||
        create_graphics_pipelines == nullptr || destroy_pipeline == nullptr || create_framebuffer == nullptr ||
        destroy_framebuffer == nullptr || device_wait_idle == nullptr) {
        return std::nullopt;
    }

    const auto create_shader = [create_shader_module,
                                &device](const std::vector<std::uint32_t>& words) -> std::optional<VkShaderModule> {
        const VkShaderModuleCreateInfo create_info{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .codeSize = words.size() * sizeof(std::uint32_t),
            .pCode = words.data(),
        };
        VkShaderModule shader_module{VK_NULL_HANDLE};
        if (create_shader_module(device.state_->device, &create_info, nullptr, &shader_module) != VK_SUCCESS) {
            return std::nullopt;
        }
        return shader_module;
    };
    const auto vertex_shader = create_shader(*vertex_words);
    if (!vertex_shader.has_value()) {
        return std::nullopt;
    }
    const auto fragment_shader = create_shader(*fragment_words);
    if (!fragment_shader.has_value()) {
        destroy_shader_module(device.state_->device, *vertex_shader, nullptr);
        return std::nullopt;
    }
    const auto destroy_shaders = [&device, destroy_shader_module, vertex_shader, fragment_shader]() noexcept {
        destroy_shader_module(device.state_->device, *fragment_shader, nullptr);
        destroy_shader_module(device.state_->device, *vertex_shader, nullptr);
    };

    const VkAttachmentDescription color_attachment{
        .flags = 0U,
        .format = swapchain.state_->image_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    const VkAttachmentReference color_attachment_reference{
        .attachment = 0U,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    const VkSubpassDescription subpass{
        .flags = 0U,
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .inputAttachmentCount = 0U,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1U,
        .pColorAttachments = &color_attachment_reference,
        .pResolveAttachments = nullptr,
        .pDepthStencilAttachment = nullptr,
        .preserveAttachmentCount = 0U,
        .pPreserveAttachments = nullptr,
    };
    constexpr std::array<VkSubpassDependency, 2U> dependencies{
        VkSubpassDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0U,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0U,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = 0U,
        },
        VkSubpassDependency{
            .srcSubpass = 0U,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = 0U,
            .dependencyFlags = 0U,
        },
    };
    const VkRenderPassCreateInfo render_pass_create_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .attachmentCount = 1U,
        .pAttachments = &color_attachment,
        .subpassCount = 1U,
        .pSubpasses = &subpass,
        .dependencyCount = static_cast<std::uint32_t>(dependencies.size()),
        .pDependencies = dependencies.data(),
    };

    auto state = std::make_unique<State>();
    state->device_state = device.state_;
    state->swapchain_state = swapchain.state_;
    state->destroy_framebuffer = destroy_framebuffer;
    state->destroy_pipeline = destroy_pipeline;
    state->destroy_pipeline_layout = destroy_pipeline_layout;
    state->destroy_render_pass = destroy_render_pass;
    state->device_wait_idle = device_wait_idle;
    if (create_render_pass(device.state_->device, &render_pass_create_info, nullptr, &state->render_pass) !=
        VK_SUCCESS) {
        destroy_shaders();
        return std::nullopt;
    }

    const VkPipelineLayoutCreateInfo pipeline_layout_create_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .setLayoutCount = 0U,
        .pSetLayouts = nullptr,
        .pushConstantRangeCount = 0U,
        .pPushConstantRanges = nullptr,
    };
    if (create_pipeline_layout(device.state_->device, &pipeline_layout_create_info, nullptr, &state->pipeline_layout) !=
        VK_SUCCESS) {
        destroy_shaders();
        return std::nullopt;
    }

    constexpr char k_entry_point[]{"main"};
    const std::array<VkPipelineShaderStageCreateInfo, 2U> shader_stages{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = *vertex_shader,
            .pName = k_entry_point,
            .pSpecializationInfo = nullptr,
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = *fragment_shader,
            .pName = k_entry_point,
            .pSpecializationInfo = nullptr,
        },
    };
    const VkPipelineVertexInputStateCreateInfo vertex_input_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .vertexBindingDescriptionCount = 0U,
        .pVertexBindingDescriptions = nullptr,
        .vertexAttributeDescriptionCount = 0U,
        .pVertexAttributeDescriptions = nullptr,
    };
    const VkPipelineInputAssemblyStateCreateInfo input_assembly_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };
    const VkPipelineViewportStateCreateInfo viewport_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .viewportCount = 1U,
        .pViewports = nullptr,
        .scissorCount = 1U,
        .pScissors = nullptr,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0F,
        .depthBiasClamp = 0.0F,
        .depthBiasSlopeFactor = 0.0F,
        .lineWidth = 1.0F,
    };
    const VkPipelineMultisampleStateCreateInfo multisample_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0F,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE,
    };
    const VkPipelineColorBlendAttachmentState color_blend_attachment{
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo color_blend_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1U,
        .pAttachments = &color_blend_attachment,
        .blendConstants = {0.0F, 0.0F, 0.0F, 0.0F},
    };
    constexpr std::array<VkDynamicState, 2U> dynamic_states{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamic_state{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .dynamicStateCount = static_cast<std::uint32_t>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };
    const VkGraphicsPipelineCreateInfo pipeline_create_info{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0U,
        .stageCount = static_cast<std::uint32_t>(shader_stages.size()),
        .pStages = shader_stages.data(),
        .pVertexInputState = &vertex_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pTessellationState = nullptr,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization_state,
        .pMultisampleState = &multisample_state,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &color_blend_state,
        .pDynamicState = &dynamic_state,
        .layout = state->pipeline_layout,
        .renderPass = state->render_pass,
        .subpass = 0U,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };
    if (create_graphics_pipelines(device.state_->device, VK_NULL_HANDLE, 1U, &pipeline_create_info, nullptr,
                                  &state->pipeline) != VK_SUCCESS) {
        destroy_shaders();
        return std::nullopt;
    }
    destroy_shaders();

    state->framebuffers.reserve(swapchain.state_->image_views.size());
    for (const VkImageView image_view : swapchain.state_->image_views) {
        const VkFramebufferCreateInfo framebuffer_create_info{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0U,
            .renderPass = state->render_pass,
            .attachmentCount = 1U,
            .pAttachments = &image_view,
            .width = swapchain.state_->capabilities.width,
            .height = swapchain.state_->capabilities.height,
            .layers = 1U,
        };
        VkFramebuffer framebuffer{VK_NULL_HANDLE};
        if (create_framebuffer(device.state_->device, &framebuffer_create_info, nullptr, &framebuffer) != VK_SUCCESS) {
            return std::nullopt;
        }
        state->framebuffers.push_back(framebuffer);
    }
    state->capabilities.available = true;
    return VulkanTrianglePipeline{std::move(state)};
}

const VulkanTrianglePipelineCapabilities& VulkanTrianglePipeline::capabilities() const noexcept {
    static const VulkanTrianglePipelineCapabilities unavailable{};
    return state_ != nullptr ? state_->capabilities : unavailable;
}

bool VulkanSwapchain::present_clear(const VulkanDevice& device, VulkanCommandContext& command_context,
                                    VulkanFrameSync& frame_sync, const VulkanClearColor& color) noexcept {
    if (state_ == nullptr || command_context.state_ == nullptr || frame_sync.state_ == nullptr ||
        device.state_ == nullptr || state_->device_state.get() != device.state_.get() ||
        command_context.state_->device_state.get() != device.state_.get() ||
        frame_sync.state_->device_state.get() != device.state_.get() || state_->images.empty() ||
        command_context.state_->command_buffers.empty() || frame_sync.state_->image_available_semaphores.empty() ||
        frame_sync.state_->render_finished_semaphores.empty() || frame_sync.state_->in_flight_fences.empty() ||
        state_->acquire_next_image == nullptr || state_->queue_submit == nullptr || state_->queue_present == nullptr ||
        command_context.state_->reset_command_buffer == nullptr ||
        command_context.state_->begin_command_buffer == nullptr ||
        command_context.state_->end_command_buffer == nullptr ||
        command_context.state_->cmd_pipeline_barrier == nullptr ||
        command_context.state_->cmd_clear_color_image == nullptr || frame_sync.state_->wait_for_fences == nullptr ||
        frame_sync.state_->reset_fences == nullptr) {
        return false;
    }

    const VkFence fence = frame_sync.state_->in_flight_fences.front();
    if (frame_sync.state_->wait_for_fences(device.state_->device, 1U, &fence, VK_TRUE,
                                           std::numeric_limits<std::uint64_t>::max()) != VK_SUCCESS) {
        return false;
    }

    std::uint32_t image_index{0U};
    const VkResult acquire_result =
        state_->acquire_next_image(device.state_->device, state_->swapchain, std::numeric_limits<std::uint64_t>::max(),
                                   frame_sync.state_->image_available_semaphores.front(), VK_NULL_HANDLE, &image_index);
    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR || acquire_result == VK_SUBOPTIMAL_KHR) {
        state_->recreation_required = true;
        return false;
    }
    if (acquire_result != VK_SUCCESS || image_index >= state_->images.size()) {
        return false;
    }

    VkCommandBuffer command_buffer = command_context.state_->command_buffers.front();
    if (command_context.state_->reset_command_buffer(command_buffer, 0U) != VK_SUCCESS) {
        return false;
    }
    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    if (command_context.state_->begin_command_buffer(command_buffer, &begin_info) != VK_SUCCESS) {
        return false;
    }
    const VkImageSubresourceRange range{
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0U,
        .levelCount = 1U,
        .baseArrayLayer = 0U,
        .layerCount = 1U,
    };
    const VkImageMemoryBarrier to_transfer{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0U,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = state_->images[image_index],
        .subresourceRange = range,
    };
    command_context.state_->cmd_pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 0U, nullptr, 1U,
                                                 &to_transfer);
    const VkClearColorValue clear_value{{color.red, color.green, color.blue, color.alpha}};
    command_context.state_->cmd_clear_color_image(command_buffer, state_->images[image_index],
                                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_value, 1U, &range);
    const VkImageMemoryBarrier to_present{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = 0U,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = state_->images[image_index],
        .subresourceRange = range,
    };
    command_context.state_->cmd_pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0U, 0U, nullptr, 0U, nullptr, 1U,
                                                 &to_present);
    if (command_context.state_->end_command_buffer(command_buffer) != VK_SUCCESS) {
        return false;
    }
    // 仅在命令已准备提交时重置栅栏；获取或录制失败不会让下一帧永久等待未发信号的栅栏。
    if (frame_sync.state_->reset_fences(device.state_->device, 1U, &fence) != VK_SUCCESS) {
        return false;
    }

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkSemaphore wait_semaphore = frame_sync.state_->image_available_semaphores.front();
    const VkSemaphore signal_semaphore = frame_sync.state_->render_finished_semaphores.front();
    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1U,
        .pWaitSemaphores = &wait_semaphore,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1U,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 1U,
        .pSignalSemaphores = &signal_semaphore,
    };
    if (state_->queue_submit(device.state_->graphics_queue, 1U, &submit_info, fence) != VK_SUCCESS) {
        return false;
    }
    command_context.state_->has_submitted_work = true;
    frame_sync.state_->has_submitted_work = true;
    state_->has_submitted_work = true;

    const VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1U,
        .pWaitSemaphores = &signal_semaphore,
        .swapchainCount = 1U,
        .pSwapchains = &state_->swapchain,
        .pImageIndices = &image_index,
        .pResults = nullptr,
    };
    const VkResult present_result = state_->queue_present(device.state_->graphics_queue, &present_info);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
        state_->recreation_required = true;
        return false;
    }
    return present_result == VK_SUCCESS;
}

bool VulkanSwapchain::present_triangle(const VulkanDevice& device, VulkanCommandContext& command_context,
                                       VulkanFrameSync& frame_sync, const VulkanTrianglePipeline& pipeline,
                                       const VulkanClearColor& clear_color) noexcept {
    if (state_ == nullptr || pipeline.state_ == nullptr || command_context.state_ == nullptr ||
        frame_sync.state_ == nullptr || device.state_ == nullptr || state_->device_state.get() != device.state_.get() ||
        pipeline.state_->device_state.get() != device.state_.get() ||
        pipeline.state_->swapchain_state.get() != state_.get() || state_->images.empty() ||
        pipeline.state_->framebuffers.empty() || command_context.state_->command_buffers.empty() ||
        frame_sync.state_->image_available_semaphores.empty() ||
        frame_sync.state_->render_finished_semaphores.empty() || frame_sync.state_->in_flight_fences.empty() ||
        state_->acquire_next_image == nullptr || state_->queue_submit == nullptr || state_->queue_present == nullptr ||
        command_context.state_->reset_command_buffer == nullptr ||
        command_context.state_->begin_command_buffer == nullptr ||
        command_context.state_->end_command_buffer == nullptr ||
        command_context.state_->cmd_begin_render_pass == nullptr ||
        command_context.state_->cmd_end_render_pass == nullptr ||
        command_context.state_->cmd_bind_pipeline == nullptr || command_context.state_->cmd_set_viewport == nullptr ||
        command_context.state_->cmd_set_scissor == nullptr || command_context.state_->cmd_draw == nullptr ||
        frame_sync.state_->wait_for_fences == nullptr || frame_sync.state_->reset_fences == nullptr) {
        return false;
    }

    const VkFence fence = frame_sync.state_->in_flight_fences.front();
    if (frame_sync.state_->wait_for_fences(device.state_->device, 1U, &fence, VK_TRUE,
                                           std::numeric_limits<std::uint64_t>::max()) != VK_SUCCESS) {
        return false;
    }

    std::uint32_t image_index{0U};
    const VkResult acquire_result =
        state_->acquire_next_image(device.state_->device, state_->swapchain, std::numeric_limits<std::uint64_t>::max(),
                                   frame_sync.state_->image_available_semaphores.front(), VK_NULL_HANDLE, &image_index);
    if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR || acquire_result == VK_SUBOPTIMAL_KHR) {
        state_->recreation_required = true;
        return false;
    }
    if (acquire_result != VK_SUCCESS || image_index >= state_->images.size() ||
        image_index >= pipeline.state_->framebuffers.size()) {
        return false;
    }

    const VkCommandBuffer command_buffer = command_context.state_->command_buffers.front();
    if (command_context.state_->reset_command_buffer(command_buffer, 0U) != VK_SUCCESS) {
        return false;
    }
    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    if (command_context.state_->begin_command_buffer(command_buffer, &begin_info) != VK_SUCCESS) {
        return false;
    }
    const VkClearValue clear_value{
        .color = {{clear_color.red, clear_color.green, clear_color.blue, clear_color.alpha}}};
    const VkRenderPassBeginInfo render_pass_begin_info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .pNext = nullptr,
        .renderPass = pipeline.state_->render_pass,
        .framebuffer = pipeline.state_->framebuffers[image_index],
        .renderArea =
            {
                .offset = {.x = 0, .y = 0},
                .extent = {.width = state_->capabilities.width, .height = state_->capabilities.height},
            },
        .clearValueCount = 1U,
        .pClearValues = &clear_value,
    };
    command_context.state_->cmd_begin_render_pass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(state_->capabilities.width),
        .height = static_cast<float>(state_->capabilities.height),
        .minDepth = 0.0F,
        .maxDepth = 1.0F,
    };
    const VkRect2D scissor{
        .offset = {.x = 0, .y = 0},
        .extent = {.width = state_->capabilities.width, .height = state_->capabilities.height},
    };
    command_context.state_->cmd_set_viewport(command_buffer, 0U, 1U, &viewport);
    command_context.state_->cmd_set_scissor(command_buffer, 0U, 1U, &scissor);
    command_context.state_->cmd_bind_pipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              pipeline.state_->pipeline);
    command_context.state_->cmd_draw(command_buffer, 3U, 1U, 0U, 0U);
    command_context.state_->cmd_end_render_pass(command_buffer);
    if (command_context.state_->end_command_buffer(command_buffer) != VK_SUCCESS) {
        return false;
    }
    if (frame_sync.state_->reset_fences(device.state_->device, 1U, &fence) != VK_SUCCESS) {
        return false;
    }

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSemaphore wait_semaphore = frame_sync.state_->image_available_semaphores.front();
    const VkSemaphore signal_semaphore = frame_sync.state_->render_finished_semaphores.front();
    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1U,
        .pWaitSemaphores = &wait_semaphore,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1U,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 1U,
        .pSignalSemaphores = &signal_semaphore,
    };
    if (state_->queue_submit(device.state_->graphics_queue, 1U, &submit_info, fence) != VK_SUCCESS) {
        return false;
    }
    command_context.state_->has_submitted_work = true;
    frame_sync.state_->has_submitted_work = true;
    state_->has_submitted_work = true;
    pipeline.state_->has_submitted_work = true;

    const VkPresentInfoKHR present_info{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1U,
        .pWaitSemaphores = &signal_semaphore,
        .swapchainCount = 1U,
        .pSwapchains = &state_->swapchain,
        .pImageIndices = &image_index,
        .pResults = nullptr,
    };
    const VkResult present_result = state_->queue_present(device.state_->graphics_queue, &present_info);
    if (present_result == VK_ERROR_OUT_OF_DATE_KHR || present_result == VK_SUBOPTIMAL_KHR) {
        state_->recreation_required = true;
        return false;
    }
    return present_result == VK_SUCCESS;
}

const VulkanSwapchainCapabilities& VulkanSwapchain::capabilities() const noexcept {
    static const VulkanSwapchainCapabilities unavailable{};
    return state_ != nullptr ? state_->capabilities : unavailable;
}

bool VulkanSwapchain::requires_recreation() const noexcept {
    return state_ != nullptr && state_->recreation_required;
}

}  // namespace gisengine::rhi
