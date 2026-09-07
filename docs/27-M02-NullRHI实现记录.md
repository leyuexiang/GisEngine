# 27 M02 Null RHI 实现记录

## 1. 范围

本文记录 `T-RHI-001` 的首版实现：渲染硬件接口（RHI，Rendering Hardware Interface）资源描述、类型安全句柄、设备能力模型和无 GPU 依赖的 Null RHI（空渲染接口）。

## 2. 已交付内容

- `IRenderDevice`：设备能力查询、Buffer/Texture/Sampler/Pipeline 创建和销毁、帧开始与提交接口。
- 不可变描述：资源创建参数按值保存，后续后端可基于描述重建资源。
- 类型安全句柄：Buffer、Texture、Sampler 和 Pipeline 使用不同标签类型，禁止跨资源类别误传。
- 代数校验：销毁资源后递增代数，旧句柄不能再次销毁或访问。
- `DeviceCapabilities`：表达后端、Compute、时间戳、间接绘制、纹理尺寸和颜色附件上限。
- `NullRenderDevice`：验证资源描述与帧生命周期，不创建 GPU 对象，适合无驱动自动化测试。
- `ResourceState`、Fence 和设备状态迁移：支持帧内资源状态转换、Buffer 拷贝命令、提交完成查询与设备丢失/恢复模拟。

## 3. 当前约束

- RHI 对象属于渲染线程；Null RHI 当前不提供并发访问保证。
- 当前资源屏障仅以状态转换契约验证，不生成真实 API 屏障；Fence 在 Null RHI 中于提交时立即完成。设备丢失会失效全部现有资源句柄；真实后端的可恢复资源重建仍待后续资源注册表实现。
- 当前 Pipeline 描述只保存 Shader 标识；反射、绑定布局和跨后端编译由 `T-SHADER-001` 接管。

## 4. 验收

无 GPU 的 CTest 冒烟覆盖 Buffer 创建、销毁、旧句柄拒绝、帧提交和 Null 能力模型。完整 GoogleTest 用例已随系统 GoogleTest 切换入口提供。

## 5. 下一步

1. `T-RHI-003`：扩展 Null RHI 命令验证器，覆盖更多资源冲突和非法状态。
2. `T-VK-001`：以同一 RHI 契约实现 Vulkan 设备、交换链和清屏三角形。
