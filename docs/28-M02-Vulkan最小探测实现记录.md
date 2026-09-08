# 28 M02 Vulkan 最小探测实现记录

本文记录 `T-VK-001` 的当前实现：完成 Vulkan 运行时加载、实例创建、物理设备枚举、逻辑设备创建、Windows 可见窗口表面、呈现队列选择、交换链、图像视图、基础事件循环、图形命令池、帧同步、固定版本着色器编译链和首个彩色三角形图形管线。

## 1. 实现范围

- `VulkanInstance`：通过资源获取即初始化（RAII，Resource Acquisition Is Initialization）管理 Vulkan 实例生命周期。
- `VulkanLoader`：运行时加载系统 Vulkan 加载器；Windows 使用 `vulkan-1.dll`，Linux 使用 `libvulkan.so.1`，macOS 使用 `libvulkan.1.dylib`。WebAssembly 不尝试加载原生动态库。
- `VulkanCapabilities`：返回加载器实例版本、物理设备名称、API 版本及厂商/设备标识。
- `VulkanDevice`：按离散 GPU、集成 GPU 等设备类型优先级选择物理设备，创建逻辑设备并选择图形、计算和传输队列。
- `VulkanDeviceCapabilities`：返回选中物理设备和各队列族索引；逻辑设备持有实例状态，保证销毁顺序正确。
- `VulkanSurface`：在 Windows 创建原生窗口及 `VK_KHR_win32_surface` 表面；默认使用不可见的 1280×720 探测窗口，也支持示例创建可见窗口，并以资源获取即初始化（RAII，Resource Acquisition Is Initialization）保证 Surface、窗口和窗口类按正确顺序释放。
- `VulkanSurface::process_events()`：通过非阻塞 Windows 消息泵处理窗口事件，关闭窗口后返回 false，示例据此退出事件循环；同时记录最新客户端尺寸，区分最小化的零尺寸状态与待重建的有效尺寸。
- `VulkanCommandContext`：为图形队列创建可重置的命令池，并按请求分配主级命令缓冲区；命令缓冲区先于命令池释放，命令池再随逻辑设备释放。
- `VulkanFrameSync`：每帧创建一组图像可用信号量、渲染完成信号量和初始已发信号的在途栅栏；部分创建失败时释放当前帧对象，其余对象由资源获取即初始化（RAII，Resource Acquisition Is Initialization）统一回滚。
- `VulkanSwapchain::present_clear()`：获取交换链镜像，记录“呈现布局→传输目标布局→清除→呈现布局”转换，提交图形队列并等待呈现；当前采用单帧同步，交换链失效或设备异常时退出事件循环。
- 含 Surface 的逻辑设备创建会筛选同时支持图形和呈现的队列族；Surface 能够独立查询已选图形队列的呈现支持。
- `VulkanSwapchain`：检查并启用 `VK_KHR_swapchain` 设备扩展，协商表面格式、呈现模式、尺寸和镜像数量，获取全部交换链镜像并为其创建二维颜色图像视图。
- 交换链通过资源获取即初始化（RAII，Resource Acquisition Is Initialization）统一管理图像视图和交换链；部分图像视图创建失败时自动回滚，正常销毁时先释放图像视图，再释放交换链。
- 交换链持有设备和 Surface 内部状态，调用方提前释放外层对象时仍能保证原生资源按交换链、设备、Surface、实例的顺序销毁。
- `GisEngineShaders`：构建期使用固定版本 shaderc/glslc 将 `Shaders/triangle.vert` 与 `Shaders/triangle.frag` 编译为 SPIR-V；源码归档、提交和 SHA-256 均固定，避免不同开发机的编译器版本漂移。
- `VulkanTrianglePipeline`：通过资源获取即初始化（RAII，Resource Acquisition Is Initialization）管理着色器模块、渲染通道、管线布局、图形管线和交换链帧缓冲；管线持有交换链状态，防止帧缓冲引用已销毁的图像视图。
- `VulkanSwapchain::present_triangle()`：为每帧命令缓冲记录清屏、动态视口/裁剪区、图形管线绑定和无顶点缓冲区的三顶点绘制，再提交图形队列并呈现；获取或呈现返回 `VK_ERROR_OUT_OF_DATE_KHR`（交换链过期）或 `VK_SUBOPTIMAL_KHR`（交换链次优）时明确要求上层重建。
- `NativeSandbox`：窗口尺寸变化时先创建完整的新交换链与新三角形管线，再按“旧管线、旧交换链”顺序释放资源；最小化时暂停呈现并继续处理消息，恢复后按当前客户端尺寸重建。
- 不依赖本机 Vulkan SDK 或静态导入库，避免 SDK 缺失阻断 Null RHI 和无 GPU 自动化测试。

项目通过 CMake FetchContent 固定 Vulkan-Headers 提交；本地存在 `ThirdParty/vulkan-headers` 时优先离线构建。

## 2. 环境验证

本机 `vulkaninfo --summary` 显示 Vulkan 实例版本 `1.4.304`，并枚举到 Intel 集成显卡及 NVIDIA GeForce RTX 4060 Laptop GPU。VS 2026 Debug 与 AddressSanitizer 全量 CTest 均通过 40/40 项，其中包括隐藏窗口的清屏和三角形管线创建/呈现测试。

## 3. 降级策略与风险

- 系统没有 Vulkan 加载器、实例创建失败或没有物理设备时，`VulkanInstance::try_create()` 返回空值；上层应回退到 Null RHI 或其他后端。
- 非 Windows 平台当前不创建 Surface，调用会返回空值，测试将跳过；Linux/macOS 的原生窗口路径将在平台抽象确定后接入。
- 当前示例可以打开可见窗口并持续呈现彩色三角形；窗口缩放、最小化或交换链返回过期/次优状态时会安全重建交换链和相关图形管线，最小化期间不提交零尺寸呈现。
- 首版管线只验证无顶点缓冲区的三角形和颜色输出，尚未接入深度缓冲、顶点/索引缓冲、描述符集和可热重载的着色器资产。

## 4. 验收证据

```text
cmake --build out/build/vs-ninja-debug --target GisEngineShaders
ctest --test-dir out/build/vs-ninja-debug --output-on-failure
40/40 tests passed
```
