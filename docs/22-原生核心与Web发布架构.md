# 22 原生核心与 Web 发布架构

## 1. 目标与边界

GisEngine 的产品形态类似 UE5：C++ 原生运行时、编辑器、资产烘焙器和工具链是主产品；Web 是将选定游戏运行时编译为 WebAssembly（WASM）的发布渠道。Web 包不追求承载完整编辑器，也不改变核心对象模型。

## 2. 目标平台

| 平台 | 运行时 | 图形后端 | 线程/文件 |
|---|---|---|---|
| Windows | C++ 原生 | Vulkan、Direct3D 12 | 原生线程、文件系统、动态库 |
| Linux | C++ 原生 | Vulkan | 原生线程、文件系统、共享库 |
| macOS | C++ 原生 | Metal（后续） | 原生线程、文件系统、动态库 |
| Web | Emscripten/WASM | WebGPU、WebGL2 | Worker/Pthreads 或单线程、虚拟文件系统 |

## 3. 编译产物

```text
源资产/场景
   ↓ Asset Cooker
平台资源包（纹理压缩、网格压缩、依赖清单、符号/调试信息）
   ↓ CMake + 原生编译 / Emscripten
Windows/Linux/macOS 运行时       Web WASM + JS 胶水 + 静态资源
```

同一资源源文件不代表同一运行时包。烘焙阶段根据目标平台能力选择压缩纹理、Shader 变体、音频编码、纹理分辨率和包分块。

## 4. WebHost 适配层

`GisEngine::WebHost` 是薄适配层，负责：

- 将 Canvas、Pointer、Keyboard、Gamepad、Visibility 映射到平台事件；
- 将 Fetch、CacheStorage、IndexedDB 和虚拟文件系统注入 `IFileSystem`/`INetwork`; 
- 将 WebGPU/WebGL2 上下文交给 RHI，不在游戏逻辑中出现浏览器对象；
- 适配 `requestAnimationFrame`、页面暂停、设备像素比和音频解锁；
- 把 C++ 日志、崩溃和性能指标导出到 JavaScript 宿主。

## 5. 多线程策略

Web 默认提供单线程包，确保普通静态站点可以运行；检测到跨源隔离和 SharedArrayBuffer 后，再启用 Emscripten Pthreads。任务系统、资源生命周期和渲染快照契约在两种模式下不变。

## 6. 插件与脚本策略

原生构建支持编译期模块、静态库和签名动态库。Web 构建只允许静态注册插件或明确列出的 WASM 模块，动态库和任意文件扫描不进入浏览器包。游戏脚本首选 C++/反射接口；后续可增加 Lua、WASM 或 JavaScript 胶水，但脚本不能直接持有 RHI 原生指针。

## 7. 能力裁剪

发布配置至少分为 `NativeFull`、`WebGPU`、`WebGL2` 三档。每档通过 Feature Set 裁剪：编辑器、动态插件、原生文件观察器、Compute Pass、时间戳、压缩纹理、音频和线程。缺失能力必须有占位、CPU 备用或显式不可用状态。

## 8. 调试与发布

原生包保留符号、RenderDoc/Tracy 标记和崩溃转储；Web 包保留 source map、能力报告和构建哈希。问题报告必须记录引擎版本、目标平台、编译器、RHI、GPU、资源包版本和 Feature Set。

## 9. 不能混淆的边界

- TypeScript/JavaScript 是 WebHost 和页面集成语言，不是引擎核心语言。
- WebGPU 是发布后端，不是引擎唯一渲染抽象。
- WASM 是 C++ 的编译产物，不是替代 C++ 的主要实现语言。
- 编辑器资产格式和运行时烘焙格式分离，浏览器不承担完整导入和烘焙工作。
