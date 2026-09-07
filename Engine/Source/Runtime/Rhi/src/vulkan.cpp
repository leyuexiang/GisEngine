#include "gisengine/rhi/vulkan.h"

#include <array>
#include <bit>
#include <cstring>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define GISENGINE_VKAPI_CALL __stdcall
#define GISENGINE_CAN_LOAD_VULKAN 1
#elif defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#define GISENGINE_VKAPI_CALL
#define GISENGINE_CAN_LOAD_VULKAN 1
#else
#define GISENGINE_VKAPI_CALL
#define GISENGINE_CAN_LOAD_VULKAN 0
#endif

namespace gisengine::rhi {
namespace {

using VkFlags = std::uint32_t;
using VkInstance = void*;
using VkPhysicalDevice = void*;
using VkResult = std::int32_t;

constexpr VkResult k_vk_success{0};
constexpr std::uint32_t k_vk_structure_type_application_info{0U};
constexpr std::uint32_t k_vk_structure_type_instance_create_info{1U};
constexpr std::uint32_t k_vk_api_version_1_0{(1U << 22U)};
#ifdef _WIN32
constexpr wchar_t k_vulkan_loader_file_name[]{L"\\vulkan-1.dll"};
#endif

struct VkApplicationInfo {
    std::uint32_t s_type;
    const void* p_next;
    const char* p_application_name;
    std::uint32_t application_version;
    const char* p_engine_name;
    std::uint32_t engine_version;
    std::uint32_t api_version;
};

struct VkInstanceCreateInfo {
    std::uint32_t s_type;
    const void* p_next;
    VkFlags flags;
    const VkApplicationInfo* p_application_info;
    std::uint32_t enabled_layer_count;
    const char* const* pp_enabled_layer_names;
    std::uint32_t enabled_extension_count;
    const char* const* pp_enabled_extension_names;
};

using VkGetInstanceProcAddr = void*(GISENGINE_VKAPI_CALL*)(VkInstance instance, const char* name);
using VkEnumerateInstanceVersion = VkResult(GISENGINE_VKAPI_CALL*)(std::uint32_t* api_version);
using VkCreateInstance = VkResult(GISENGINE_VKAPI_CALL*)(const VkInstanceCreateInfo* create_info, const void* allocator,
                                                         VkInstance* instance);
using VkDestroyInstance = void(GISENGINE_VKAPI_CALL*)(VkInstance instance, const void* allocator);
using VkEnumeratePhysicalDevices = VkResult(GISENGINE_VKAPI_CALL*)(VkInstance instance, std::uint32_t* count,
                                                                   VkPhysicalDevice* physical_devices);
using VkGetPhysicalDeviceProperties = void(GISENGINE_VKAPI_CALL*)(VkPhysicalDevice physical_device, void* properties);

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
            std::memcpy(library_path.data() + directory_length, k_vulkan_loader_file_name,
                        sizeof(k_vulkan_loader_file_name));
            handle_ = static_cast<void*>(LoadLibraryExW(library_path.data(), nullptr, 0U));
        }
#elif defined(__APPLE__)
        handle_ = dlopen("libvulkan.1.dylib", RTLD_LOCAL | RTLD_NOW);
#elif GISENGINE_CAN_LOAD_VULKAN
        handle_ = dlopen("libvulkan.so.1", RTLD_LOCAL | RTLD_NOW);
#endif
        if (handle_ == nullptr) {
            return;
        }
        get_instance_proc_addr_ = load<VkGetInstanceProcAddr>("vkGetInstanceProcAddr");
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

    [[nodiscard]] VkGetInstanceProcAddr get_instance_proc_addr() const noexcept { return get_instance_proc_addr_; }

   private:
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
    VkGetInstanceProcAddr get_instance_proc_addr_{nullptr};
};

struct PhysicalDevicePropertiesStorage {
    // Vulkan 1.x 的 VkPhysicalDeviceProperties 大小远小于该上限；对齐满足标量字段写入要求。
    alignas(8) std::array<std::byte, 4096U> bytes{};
};

template <typename Value>
[[nodiscard]] Value read_properties(const PhysicalDevicePropertiesStorage& storage, std::size_t offset) noexcept {
    Value value{};
    std::memcpy(&value, storage.bytes.data() + offset, sizeof(Value));
    return value;
}

[[nodiscard]] std::string read_device_name(const PhysicalDevicePropertiesStorage& storage) {
    std::array<char, 256U> name{};
    std::memcpy(name.data(), storage.bytes.data() + 20U, name.size());
    name.back() = '\0';
    return std::string{name.data()};
}

}  // namespace

struct VulkanInstance::State {
    VulkanLoader loader;
    VkInstance instance{nullptr};
    VkDestroyInstance destroy_instance{nullptr};
    VulkanCapabilities capabilities;

    ~State() {
        if (instance != nullptr && destroy_instance != nullptr) {
            destroy_instance(instance, nullptr);
        }
    }
};

VulkanInstance::VulkanInstance(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}

VulkanInstance::VulkanInstance(VulkanInstance&& other) noexcept : state_(std::move(other.state_)) {}

VulkanInstance& VulkanInstance::operator=(VulkanInstance&& other) noexcept {
    if (this != &other) {
        state_ = std::move(other.state_);
    }
    return *this;
}

VulkanInstance::~VulkanInstance() = default;

std::optional<VulkanInstance> VulkanInstance::try_create() {
    auto state = std::make_unique<State>();
    if (!state->loader) {
        return std::nullopt;
    }

    const auto get_instance_proc_addr = state->loader.get_instance_proc_addr();
    const auto create_instance =
        reinterpret_cast<VkCreateInstance>(get_instance_proc_addr(nullptr, "vkCreateInstance"));
    if (create_instance == nullptr) {
        return std::nullopt;
    }

    std::uint32_t loader_api_version{k_vk_api_version_1_0};
    const auto enumerate_instance_version =
        reinterpret_cast<VkEnumerateInstanceVersion>(get_instance_proc_addr(nullptr, "vkEnumerateInstanceVersion"));
    if (enumerate_instance_version != nullptr && enumerate_instance_version(&loader_api_version) != k_vk_success) {
        loader_api_version = k_vk_api_version_1_0;
    }

    const VkApplicationInfo application_info{
        .s_type = k_vk_structure_type_application_info,
        .p_next = nullptr,
        .p_application_name = "GisEngine Vulkan Probe",
        .application_version = 1U,
        .p_engine_name = "GisEngine",
        .engine_version = 1U,
        .api_version = k_vk_api_version_1_0,
    };
    const VkInstanceCreateInfo create_info{
        .s_type = k_vk_structure_type_instance_create_info,
        .p_next = nullptr,
        .flags = 0U,
        .p_application_info = &application_info,
        .enabled_layer_count = 0U,
        .pp_enabled_layer_names = nullptr,
        .enabled_extension_count = 0U,
        .pp_enabled_extension_names = nullptr,
    };
    if (create_instance(&create_info, nullptr, &state->instance) != k_vk_success) {
        return std::nullopt;
    }

    state->destroy_instance =
        reinterpret_cast<VkDestroyInstance>(get_instance_proc_addr(state->instance, "vkDestroyInstance"));
    const auto enumerate_physical_devices = reinterpret_cast<VkEnumeratePhysicalDevices>(
        get_instance_proc_addr(state->instance, "vkEnumeratePhysicalDevices"));
    const auto get_physical_device_properties = reinterpret_cast<VkGetPhysicalDeviceProperties>(
        get_instance_proc_addr(state->instance, "vkGetPhysicalDeviceProperties"));
    if (state->destroy_instance == nullptr || enumerate_physical_devices == nullptr ||
        get_physical_device_properties == nullptr) {
        return std::nullopt;
    }

    std::uint32_t device_count{0U};
    if (enumerate_physical_devices(state->instance, &device_count, nullptr) != k_vk_success || device_count == 0U) {
        return std::nullopt;
    }
    std::vector<VkPhysicalDevice> physical_devices(device_count);
    if (enumerate_physical_devices(state->instance, &device_count, physical_devices.data()) != k_vk_success) {
        return std::nullopt;
    }

    state->capabilities.available = true;
    state->capabilities.instance_api_version = loader_api_version;
    state->capabilities.physical_devices.reserve(device_count);
    for (std::uint32_t index{0U}; index < device_count; ++index) {
        PhysicalDevicePropertiesStorage properties;
        get_physical_device_properties(physical_devices[index], properties.bytes.data());
        state->capabilities.physical_devices.push_back({
            .name = read_device_name(properties),
            .api_version = read_properties<std::uint32_t>(properties, 0U),
            .vendor_id = read_properties<std::uint32_t>(properties, 8U),
            .device_id = read_properties<std::uint32_t>(properties, 12U),
        });
    }
    return VulkanInstance{std::move(state)};
}

const VulkanCapabilities& VulkanInstance::capabilities() const noexcept {
    static const VulkanCapabilities unavailable{};
    return state_ != nullptr ? state_->capabilities : unavailable;
}

}  // namespace gisengine::rhi
