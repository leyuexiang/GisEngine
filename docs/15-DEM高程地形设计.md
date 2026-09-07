# 15 DEM 高程地形设计

## 1. 设计目标

支持数字高程模型（DEM，Digital Elevation Model）瓦片与影像瓦片组合，提供高程网格、法线、LOD、裂缝修补、拾取和流式生命周期。DEM 是地形数据层，不直接拥有相机或渲染设备。

## 2. 高程数据模型

```ts
interface HeightField {
  width: number;
  height: number;
  values: Float32Array;
  noDataValue?: number;
  min: number;
  max: number;
  verticalDatum: string;
}
```

首版支持规则网格和 65×65 等常见采样尺寸；后续允许量化网格、压缩高程和不规则网格，通过 `HeightDecoder` 扩展。

## 3. TilePayload 扩展

```ts
interface TerrainTilePayload extends TilePayload {
  height?: HeightField;
  imagery?: DecodedImage;
  geometricError: number;
  borderSamples?: BorderSamples;
}
```

影像与高程可以来自不同图层，必须通过同一 `TileKey`、空间参考和边界策略对齐；任一数据源缺失都要有独立降级。

## 4. 网格生成

地形网格生成流程：读取采样 → 替换无数据值 → 计算地理位置 → 应用高程 → 计算局部包围体 → 填充 UV → 生成索引 → 计算法线/边缘修补。

```ts
interface TerrainMeshBuilder {
  build(input: HeightField, frame: LocalFrame, bounds: GeoBounds): DecodedMesh;
}
```

规则网格作为稳定基线；量化网格和 GPU 顶点生成作为后续优化，不改变 `DecodedMesh` 输出契约。

## 5. LOD 与裂缝

每个节点保存几何误差、最大高程、父节点和四个邻居的 LOD。相邻节点等级差超过 1 时使用裙边、边缘索引变体或过渡顶点。父节点必须继续可见，直到所有必要子节点驻留。

## 6. 法线策略

- CPU 法线：Worker 中共享边界采样，适用于 WebGL2 和低端设备。
- GPU 法线：Compute Shader 从高度纹理/网格计算，适用于 WebGPU 高能力等级。
- 法线跨瓦片使用边缘样本，避免接缝处光照断裂。

## 7. 高程瓦片分裂与级联

当一个高程瓦片被拆成子瓦片时，父节点的边界样本必须分发给子节点；子节点完成后更新父节点邻居关系和可见性。所有级联更新使用节点版本号，防止旧任务覆盖新网格。

## 8. GPU 资源

地形顶点和索引使用持久化缓冲区；`TerrainBufferPool` 负责固定尺寸块分配、归还和延迟回收。不能每帧重新创建 WebGPU Buffer。纹理可使用数组、图集或独立纹理，由能力探测和图层格式决定。

## 9. 高程与物理/拾取

地形提供高度查询和射线相交接口。近地物理只依赖局部碰撞网格，不读取 GPU Buffer；远处只提供近似高度，避免把流式渲染资源强行变成物理资源。

## 10. 测试与验收

- 高程边界、无数据值、负海拔和极值通过测试。
- 影像与 DEM 在平面和椭球模式下对齐。
- LOD 变化不出现洞、裂缝和高度跳变。
- 取消/失败/重试不会泄漏 HeightField 或 GPU Buffer。
- 高程查询和射线拾取在重定位前后保持一致。
