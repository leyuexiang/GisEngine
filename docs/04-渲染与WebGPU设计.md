# 04 渲染与 WebGPU 设计

## 1. 目标

以 WebGPU 为主后端，提供可验证的 WebGL2 降级路径。渲染模块只消费 RenderSnapshot，不直接遍历业务对象。

## 2. RHI 抽象

RHI 暴露资源描述而不是具体 API 对象：

```ts
interface RenderDevice {
  capabilities: DeviceCapabilities;
  createBuffer(desc: BufferDesc): BufferHandle;
  createTexture(desc: TextureDesc): TextureHandle;
  createSampler(desc: SamplerDesc): SamplerHandle;
  createPipeline(desc: PipelineDesc): PipelineHandle;
  beginFrame(): FrameContext;
  submit(frame: FrameContext): void;
}
```

句柄包含资源类型和 generation；设备丢失后所有句柄失效，由资源管理器重新上传。

## 3. WebGPU 初始化

初始化流程：请求 `GPUAdapter` → 评估 limits/features → 请求 `GPUDevice` → 配置 Canvas → 创建全局 Bind Group Layout → 注册 uncaptured error 和 device lost 处理。

能力等级：

| 等级 | 内容 |
|---|---|
| W0 | 基础渲染、深度、纹理、采样器 |
| W1 | Storage Buffer、Indirect Draw、纹理压缩 |
| W2 | Compute 裁剪、GPU 粒子、时间戳查询 |

缺失能力必须有替代路径，而不是在运行时崩溃。

## 4. RenderGraph

RenderGraph 描述资源读写和 Pass 依赖，编译后生成 WebGPU CommandEncoder；同一图结构缓存编译结果，窗口尺寸变化时只重建受影响资源。

首版 Pass：

```text
Shadow → DepthPrepass → OpaquePBR → Transparent → Sky → PostProcess → UI
```

后续可插入 SSR、SSAO、体积雾和 Compute 后处理，而不改变应用层调用方式。

## 5. 数据布局

统一使用 16 字节对齐的 Uniform/Storage 数据。每帧、材质、对象三类绑定组约定如下：

| Group | 内容 |
|---|---|
| 0 | 相机矩阵、时间、环境光、视口 |
| 1 | 材质参数、纹理和采样器 |
| 2 | 模型矩阵、对象 ID、实例数据 |

对象数据优先放 Storage Buffer，使用动态偏移或对象索引，减少 Bind Group 创建。

## 6. Shader 与材质

WGSL 是 WebGPU 主语言；WebGL2 维护等价 GLSL 变体。材质由固定 PBR 参数加可选扩展组成，着色器变体通过稳定哈希缓存。

基础材质参数：基础颜色、金属度、粗糙度、法线、遮挡、自发光、透明模式和双面模式。

## 7. 裁剪、排序和绘制

第一版 CPU 视锥裁剪 + 材质排序；第二版增加 GPU Compute 裁剪和间接绘制。所有物体都提供包围盒/包围球，透明物体按深度排序。

性能目标：减少每帧对象分配，持久化动态 Buffer，合并相同管线和材质的绘制。

## 8. 坐标与精度

CPU 世界坐标可用双精度；GPU 使用相机附近的局部单精度坐标。大世界采用原点重定位，重定位事件必须同步物理、粒子、音频和脚本。

地球级渲染的椭球、ECEF、ENU、反向 Z 和对数深度策略见 [14-地球椭球与空间参考设计](./14-地球椭球与空间参考设计.md) 与 [18-精度深度与可视化质量设计](./18-精度深度与可视化质量设计.md)。

## 9. 降级策略

- WebGPU 不可用：切换 WebGL2。
- 无压缩纹理：使用 PNG/JPEG 或未压缩 RGBA8。
- 无 Compute：使用 CPU 裁剪和粒子。
- 无时间戳查询：使用 CPU 帧耗时近似。

## 10. 验收标准

- WebGPU 和 WebGL2 渲染同一测试场景。
- 设备丢失后能提示并恢复资源。
- RenderGraph 无未声明的资源读写冲突。
- 帧内 GPU 资源创建数量接近零。
- 低端设备能自动降低分辨率、阴影和后处理等级。
