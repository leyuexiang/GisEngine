# GisEngine

GisEngine 是一套类似 Unreal Engine 5 的原生优先（Native-first）3D 游戏引擎规划仓库。引擎核心、编辑器、资产工具和游戏运行时以 C++20 开发；Web 不是核心运行环境，而是通过 Emscripten 编译出的发布目标（WebAssembly + WebGPU/WebGL2）。

## 文档入口

从 [docs/00-文档索引与阅读指南.md](./docs/00-文档索引与阅读指南.md) 开始。

核心文档：

- [里程碑规划](./docs/01-里程碑规划.md)
- [总体架构设计](./docs/02-总体架构设计.md)
- [渲染与 WebGPU 设计](./docs/04-渲染与WebGPU设计.md)
- [世界流式与四叉树设计](./docs/06-世界流式与四叉树设计.md)
- [开发任务分解](./docs/11-开发任务分解.md)

第二季扩展：

- [第二季参考映射与扩展总览](./docs/13-第二季参考映射与扩展总览.md)
- [地球椭球与空间参考设计](./docs/14-地球椭球与空间参考设计.md)
- [DEM 高程地形设计](./docs/15-DEM高程地形设计.md)
- [图层与数据格式设计](./docs/16-图层与数据格式设计.md)
- [矢量白膜与几何生成设计](./docs/17-矢量白膜与几何生成设计.md)
- [第二季扩展任务分解](./docs/21-第二季扩展任务分解.md)
- [原生核心与 Web 发布架构](./docs/22-原生核心与Web发布架构.md)

## 当前状态

- 阶段：M00 工程基线、M01 数学与实体句柄首版、`T-RHI-001/002` 的 Null RHI 已完成；M09～M14 第二季扩展规划与原生核心路线已在 ADR-001 固化
- 代码：已建立格式、静态检查、警告门禁、Sanitizer 构建入口，以及数学、实体句柄、ECS、Transform 场景层级、日志/事件/内存统计和 Null RHI
- 测试：GoogleTest 1.17.0 固定源码已随仓库提供，覆盖核心、实体、ECS、数学、场景、RHI 与 Vulkan 探测；VS 2026 Debug 与 AddressSanitizer 的 24 项 CTest 均已通过，MinGW Debug 冒烟测试已通过
- 文档：架构、模块、里程碑、任务分解、原生/Web 发布边界、质量配置和 M01/M02 实施记录已建立
- 当前推进：Vulkan 逻辑设备与队列选择已完成（含固定 Vulkan-Headers 依赖）；下一步接入窗口表面与交换链

## 设计约束

- 核心运行时、编辑器和资产管线不依赖浏览器 DOM、React 或 Web API。
- 原生图形后端按 Vulkan、Direct3D 12、Metal 演进；WebGPU 是 Web 发布后端，WebGL2 是浏览器兼容后端。
- C++ 任务系统负责原生线程池；Web 构建将任务映射到 Web Worker 和浏览器异步 API。
- 资源先通过原生 Asset Cooker（资产烘焙器）生成发布包，Web 端只读取已烘焙资源。
- 地图与 GIS 能力以插件形式加入，不锁死游戏引擎核心。
