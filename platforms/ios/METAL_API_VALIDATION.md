# Metal API Validation 覆盖说明

本文记录 WhatsCanvas Metal backend 的验证范围、测试入口，以及哪些改动需要重新执行真机检查。“全部 API”在这里指项目实际暴露和调用的 Metal backend API，不是 Apple Metal SDK 中与本项目无关的全部接口。稳定 v1 的代表性真机验收已经完成。

## 当前结论

原有 22 个 Metal 测试主要覆盖画面和渲染管线，不等于全部公开 API 已执行。审计后新增 `WhatsCanvasMetalApiContractTests`，并把它加入默认开启 Metal API Validation 的 CTest 集合。当前门禁包含 23 个 Metal 测试。

公开的 `MetalRenderDevice` / `IRenderDevice` Metal 合约入口均有测试调用；正常运行中可枚举的管线、混合和 sampler 分支覆盖如下：

- 必需 Metal pipeline：10/10 创建；对应绘制/滤镜路径均由测试执行。
- Blend mode：14/14，包括原先缺少的 `Src` 和 `DstAtop`。
- Sampling filter：Linear、Nearest、MipmapLinear，3/3。
- Texture address mode：Clamp、Repeat、Mirror、Decal，4/4。
- Image upload：RGBA、RGB 转 RGBA、Alpha8，3/3；包含 mipmap 创建和更新后重建。
- Presentation：swapchain 创建、acquire、resize、普通 `presentDrawable:` 和 iOS transaction present 路径。

## 公开 API 覆盖矩阵

| API 分组 | 覆盖内容 | 主要测试 |
| --- | --- | --- |
| 可用性与生命周期 | `isAvailable`、initialize、重复 initialize、finalize、重复 finalize、abandon、后台式重建、ready/name 查询 | MetalApiContract、MetalDevice |
| Native 状态 | device、command queue、last texture、未知 selector、初始化前后和重建后的 handle | MetalApiContract |
| Render target | 有效/无效尺寸、begin/activate、资源统计 | MetalApiContract、MetalDevice、MetalRenderTarget |
| DrawList | 空列表、null target、Solid、Textured、Gradient、Clip、Shadow、统计 | MetalApiContract、MetalDrawList、MetalGradient、MetalShadowAndText |
| Command stream | null target、空 stream、Canvas 命令、render-to-image | MetalApiContract、MetalCommand、MetalRenderTarget |
| Readback | 初始化前、没有 frame、错误尺寸、成功读取、透明和半透明像素 | MetalApiContract、各像素测试 |
| Image create | RGBA、RGB、Alpha8、mipmap、null、短数据、非法尺寸和非法 channel | MetalApiContract、MetalMipmap |
| Image update | RGBA/Alpha8 局部更新、越界、null、错误资源、mipmap 重建 | MetalApiContract |
| External image | owned native handle、空/外部资源、同设备 texture wrap、wrap 后修改可见 | MetalApiContract、MetalWrapExternal |
| Clip mask | 无效路径、单路径、多路径组合、实际采样 | MetalApiContract、MetalClip、MetalDrawList |
| Filter | Blur、颜色/颗粒调整、InnerShadow、无效输入和像素对齐 | MetalFilter、MetalImageEffects、MetalFilterPixelParity |
| GPU timing | disabled、begin/end、有效结果、disable 后清理 | MetalGpuTiming |
| Presentation | 非 Cocoa surface、CAMetalLayer、acquire、resize、present、横竖屏和前后台恢复 | MetalApiContract、iOS UI tests |

## Validation 的开启方式

CMake 中所有 Metal 标签测试都注入：

```text
MTL_DEBUG_LAYER=1
MTL_DEBUG_ERROR_MODE=0
```

iOS 共享 Scheme 的 Debug Run 和 Test 也注入同一组变量。因此从 Xcode 直接运行 Demo 或 UI tests 时，新增 shader 参数、漏绑 texture/sampler/buffer、非法 encoder 调用等问题会在开发阶段暴露。

执行完整 Metal 门禁：

```sh
ctest --test-dir <build-dir> -L metal --output-on-failure
```

## API Validation 不能代替的测试

Metal API Validation 只能检查实际执行过的 API 调用，不能证明所有 GPU、驱动和系统版本行为相同，也无法在正常环境中稳定制造以下失败：

- `MTLCreateSystemDefaultDevice`、command queue、pipeline 或 texture 因系统/OOM 创建失败。
- Metal shader compiler 或 command buffer 被驱动强制失败。
- 特定 GPU family 的硬件/驱动缺陷和性能退化。
- App Store Release 环境中的调度、温控、内存压力和长时间运行问题。

这些分支保留错误检查和安全返回。稳定 v1 已在 iPhone 12（A14）完成代表性真机验收；涉及 Metal shader、资源绑定、展示、CoreText 或生命周期的后续版本，应重新执行真机 Release 场景。扩大到更多 GPU 代际属于兼容性增强，不是当前能力缺口。API Validation 不应在正式 Release 包中常开，因为它有明显调试开销。

## 新增 Metal 能力时的门禁要求

1. 新增公开 backend API 时，在 `MetalApiContractTests` 增加成功、非法输入和重建后三类断言。
2. 新增 shader 参数时，同时更新所有 encoder 绑定，并至少执行一次真实 draw，不能只验证 pipeline 创建。
3. 新增 blend、sampler、pixel format 或 filter 枚举时，补齐每个枚举值的真实执行测试。
4. CTest 必须带 `metal` 标签并加入 Validation 集合；iOS 展示相关改动还要跑 simulator UI lifecycle 和真机 Release。
