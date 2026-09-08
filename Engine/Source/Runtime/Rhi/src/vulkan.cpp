#include "gisengine/rhi/vulkan.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <ranges>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

[[nodiscard]] std::optional<QueueSelection> select_queues(
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_queue_families, VkPhysicalDevice physical_device) {
    std::uint32_t family_count{0U};
    get_queue_families(physical_device, &family_count, nullptr);
    if (family_count == 0U) {
        return std::nullopt;
    }

    std::vector<VkQueueFamilyProperties> families(family_count);
    get_queue_families(physical_device, &family_count, families.data());
    families.resize(family_count);
    const auto graphics_queue = select_graphics_queue(families);
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
    VkDevice device{VK_NULL_HANDLE};
    VkQueue graphics_queue{VK_NULL_HANDLE};
    VkQueue compute_queue{VK_NULL_HANDLE};
    VkQueue transfer_queue{VK_NULL_HANDLE};
    PFN_vkDestroyDevice destroy_device{nullptr};
    VulkanDeviceCapabilities capabilities;

    ~State() {
        if (device != VK_NULL_HANDLE && destroy_device != nullptr) {
            destroy_device(device, nullptr);
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
        .enabledExtensionCount = 0U,
        .ppEnabledExtensionNames = nullptr,
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

VulkanDevice::VulkanDevice(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

VulkanDevice::VulkanDevice(VulkanDevice&& other) noexcept : state_(std::move(other.state_)) {}

VulkanDevice& VulkanDevice::operator=(VulkanDevice&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

VulkanDevice::~VulkanDevice() = default;

std::optional<VulkanDevice> VulkanDevice::try_create(const VulkanInstance& instance) {
    if (instance.state_ == nullptr || !instance.state_->capabilities.available) {
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
    if (get_queue_families == nullptr || get_physical_device_properties == nullptr || create_device == nullptr ||
        get_device_proc_addr == nullptr) {
        return std::nullopt;
    }

    std::optional<PhysicalDeviceSelection> selected_device;
    for (const VkPhysicalDevice physical_device : instance.state_->physical_devices) {
        const auto queues = select_queues(get_queue_families, physical_device);
        if (!queues.has_value()) {
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
    if (!selected_device.has_value()) {
        return std::nullopt;
    }

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
        .enabledExtensionCount = 0U,
        .ppEnabledExtensionNames = nullptr,
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

    auto state = std::make_unique<State>();
    state->instance_state = instance.state_;
    state->device = device;
    state->destroy_device = destroy_device;
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

}  // namespace gisengine::rhi
