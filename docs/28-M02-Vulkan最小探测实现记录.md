# 28 M02 Vulkan 最小探测实现记录

本文记录 `T-VK-001` 的首个设备切片：在不引入窗口、交换链和渲染管线的前提下，完成 Vulkan 运行时加载、实例创建、物理设备枚举、逻辑设备创建和队列选择。

## 1. 实现范围

- `VulkanInstance`：通过资源获取即初始化（RAII，Resource Acquisition Is Initialization）管理 Vulkan 实例生命周期。
- `VulkanLoader`：运行时加载系统 Vulkan 加载器；Windows 使用 `vulkan-1.dll`，Linux 使用 `libvulkan.so.1`，macOS 使用 `libvulkan.1.dylib`。WebAssembly 不尝试加载原生动态库。
- `VulkanCapabilities`：返回加载器实例版本、物理设备名称、API 版本及厂商/设备标识。
- `VulkanDevice`：按离散 GPU、集成 GPU 等设备类型优先级选择物理设备，创建逻辑设备并选择图形、计算和传输队列。
- `VulkanDeviceCapabilities`：返回选中物理设备和各队列族索引；逻辑设备持有实例状态，保证销毁顺序正确。
- 不依赖本机 Vulkan SDK 或静态导入库，避免 SDK 缺失阻断 Null RHI 和无 GPU 自动化测试。

项目通过 CMake FetchContent 固定 Vulkan-Headers 提交；本地存在 `ThirdParty/vulkan-headers` 时优先离线构建。

## 2. 环境验证

本机 `vulkaninfo --summary` 显示 Vulkan 实例版本 `1.4.304`，并枚举到 Intel 集成显卡及 NVIDIA GeForce RTX 4060 Laptop GPU。新增 GoogleTest 用例在 VS 2026 Debug 构建中通过。

## 3. 降级策略与风险

- 系统没有 Vulkan 加载器、实例创建失败或没有物理设备时，`VulkanInstance::try_create()` 返回空值；上层应回退到 Null RHI 或其他后端。
- 当前未创建表面或交换链，也未提交命令和创建资源对象。
- 下一步接入窗口抽象与表面扩展，创建交换链并打通清屏通道。

## 4. 验收证据

```text
ctest --test-dir out/build/vs-ninja-debug -R "Vulkan(Instance|Device)Test" --output-on-failure
4/4 tests passed
```
