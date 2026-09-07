# 04 跨平台渲染与 WebGPU 设计

## 1. 目标

建立类似现代商业引擎的渲染硬件接口（RHI，Rendering Hardware Interface）和渲染图（RenderGraph）。Vulkan 作为首个原生验证后端，WebGPU 作为 Web 发布后端；Direct3D 12 和 Metal 按相同契约扩展，WebGL2 只提供受限兼容路径。

## 2. RHI 抽象

RHI 暴露不可变资源描述、类型安全句柄和显式命令，不向上泄露具体 API 对象：

```cpp
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    virtual const DeviceCapabilities& GetCapabilities() const = 0;
    virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
    virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
    virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;
    virtual FrameContext BeginFrame() = 0;
    virtual void Submit(FrameContext&& frame) = 0;
};
```

句柄包含类型、索引和代数（Generation）。资源销毁采用延迟队列，必须等待对应 GPU Fence 完成；设备丢失后由资源注册表按可恢复描述重建。

## 3. 后端路线

| 阶段 | 后端 | 定位 |
|---|---|---|
| R0 | Null RHI | 无 GPU 自动化测试、资源生命周期验证 |
| R1 | Vulkan | 原生参考实现，验证显式同步和资源屏障 |
| R2 | WebGPU | Emscripten Web 发行，映射浏览器能力模型 |
| R3 | Direct3D 12 | Windows 高性能与图形调试工具链 |
| R4 | Metal | macOS/iOS 原生发布 |
| 兼容 | WebGL2 | 不支持 WebGPU 的浏览器，功能等级受限 |

WebGPU 可同时通过 Dawn/wgpu-native 在原生测试，但不能代替 Vulkan/D3D12 的原生能力验证。

## 4. RenderGraph

RenderGraph 描述 Pass、逻辑资源及读写依赖，编译阶段完成拓扑排序、资源别名、屏障和队列同步，后端只负责翻译命令。

```text
Shadow → DepthPrepass → GBuffer/Forward → Lighting → Transparent → PostProcess → UI
```

渲染路径允许 Forward+、Deferred 和移动/Web 精简 Forward 三种模板。后续 SSR、SSAO、虚拟阴影、体积雾和 Compute Pass 通过功能节点加入，不改变游戏线程接口。

## 5. Shader 与材质

首个垂直切片允许核心 Shader 同时维护 HLSL/GLSL/WGSL 小型变体，以尽快验证后端。随后建立材质中间表示（Material IR）、离线编译、反射和稳定绑定布局，生成各后端目标：

- Direct3D 12：DXIL。
- Vulkan：SPIR-V。
- Metal：MSL/Metallib。
- WebGPU：WGSL。
- WebGL2：GLSL ES 3.0。

不能在确定编译器链和语义一致性前承诺“一份 Shader 自动覆盖所有后端”。Shader 工具链必须有跨后端截图测试、反射一致性测试和缓存版本号。

## 6. 线程模型

游戏线程生成只读 `RenderSnapshot`；渲染线程执行裁剪、排序、RenderGraph 编译和命令编码；RHI 队列提交 GPU。Web 单线程模式把游戏/渲染阶段串行执行，多线程 Web 模式再映射到 Emscripten Pthreads，不能改变上层数据契约。

## 7. 精度与性能

CPU 世界坐标使用双精度，渲染快照使用相机相对单精度或高低位拆分。持久化上传环、描述符/Bind Group 缓存、Pipeline Cache、实例化和间接绘制均位于 RHI 之上，以便后端选择等价能力或降级。

## 8. Web 能力等级

| 等级 | 内容 |
|---|---|
| W0 | WebGL2、CPU 裁剪、基础 PBR、传统深度 |
| W1 | WebGPU 基础渲染、Storage Buffer、压缩纹理 |
| W2 | Compute 裁剪、间接绘制、GPU 粒子、时间戳查询 |

构建系统按等级裁剪资源和 Shader 变体。缺失能力必须在加载阶段给出诊断，不能运行到某个 Pass 才崩溃。

## 9. 验收标准

- Null RHI 能验证资源、屏障和 RenderGraph 契约。
- Vulkan 与 WebGPU 渲染相同黄金场景，允许明确记录的平台色彩误差。
- 设备丢失、窗口重建和 Web Canvas 重配置可恢复。
- 正常帧内不创建长期 GPU 资源，不发生未声明资源冲突。
- WebGL2 包不会包含 Compute、间接绘制等不可用变体。
