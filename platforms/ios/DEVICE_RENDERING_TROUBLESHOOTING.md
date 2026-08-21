# iOS 真机渲染排查记录

本文记录 WhatsCanvas iOS Demo 在模拟器与真机显示不一致时的定位过程、根因、修复方式和同类风险审计。它是可长期保留的工程记录，不依赖临时任务日志。

## 现象与验证环境

- 模拟器能够显示完整 Demo，真机最初只显示黑色背景。
- 真机为 iPhone 12（A14 GPU），系统为 iOS 18.7.8；模拟器系统为 iOS 26.5，构建工具为 Xcode 26.6。
- UIKit 视图、窗口和 `CAMetalLayer` 均已创建，渲染循环也在运行，因此问题不在界面挂载或 `CADisplayLink`。
- 修复后真机与模拟器均能显示全部示例卡片，CoreText 中文与 emoji 正常，横竖屏、后台恢复和冷启动测试通过。
- Release 真机稳定在 59.2–59.9 fps，每帧 10 个 draw、10 个 command。

## 问题一：真机没有展示已渲染的 drawable

### 定位过程

1. 确认 `CAMetalLayer` 的尺寸、scale、pixel format 和 drawable 均有效。
2. 将渲染内容简化为清屏色，确认问题与 Canvas 命令内容无关。
3. 对比命令缓冲区的提交方式，发现模拟器接受 `presentDrawable:`，但当前真机路径没有把结果交给 Core Animation 显示。
4. 开启事务展示后，等待命令进入 scheduled 状态，再显式调用 drawable 的 `present`，真机开始显示。

### 修复

- iOS Demo 的 `CAMetalLayer` 设置 `presentsWithTransaction = YES`。
- 事务展示路径按 `commit` → `waitUntilScheduled` → `drawable present` 执行。
- 非事务路径仍使用 Metal 命令缓冲区的 `presentDrawable:`。
- scheduled 阶段若命令缓冲区失败，输出具体 Metal 错误并返回失败。

## 问题二：裁剪蒙版触发 GPU Address Fault

### 定位过程

展示链路修复后，真机仍只出现部分内容。为每个同步 Metal 命令缓冲区增加状态和错误输出后，裁剪蒙版阶段报告：

```text
MTLCommandBufferErrorDomain Code=3: Caused GPU Address Fault
```

逐一核对 shader 声明和 encoder 绑定后发现：裁剪蒙版复用了 `solid_fs`。该 fragment shader 无条件声明并读取 `buffer(0)` 的 `SolidUniforms`，但蒙版 raster pass 没有绑定这个 buffer。模拟器容忍了未绑定资源，A14 真机则发生页错误。

### 修复

裁剪蒙版 raster pass 在 fragment `buffer(0)` 绑定零初始化的 `SolidUniforms`。其中 clip-enabled 标志为 0，因此该 pass 不会读取嵌套裁剪纹理，同时满足 shader 对常量缓冲区的资源约定。

### 经验

- 不能以模拟器是否崩溃判断 Metal 资源绑定是否合法。
- shader 参数即使只在运行时分支中使用，也要区分“参数本身必须绑定”和“受分支保护的可选纹理”。
- 真机 GPU 故障可能只表现为黑屏或部分内容消失；同步等待后检查 `MTLCommandBuffer.status` 和 `error` 能显著缩短定位时间。

## 问题三：CoreText 渐变在 A14 上不稳定

早期方案在 textured fragment shader 中加入较大的渐变 stop 数组，并按 stop 动态索引。模拟器工作正常，但 A14 真机发生 GPU 页错误。

当前方案对“首尾 stop 位于 0 和 1 的两色、Clamp、线性渐变”在 CPU 端计算四个 quad 顶点的颜色，通过已有 per-vertex tint 传入 Metal。线性插值与该类型渐变的逐像素结果等价，同时不扩大 fragment uniform block。复杂渐变元数据仍保留，供支持逐片元渐变的后端使用。

CoreText 始终作为 iOS Demo 的文字后端，真机不再回退到 portable text。

## 问题四：诊断截图影响 60 fps

周期性将 render target 读回 PNG 会引入 GPU/CPU 同步和编码开销，真机帧率曾降至约 53 fps。截图现改为显式诊断开关：仅在 Scheme 的启动参数中加入 `--capture-frames` 时生成，正常运行不做周期性 readback。

## 问题五：关闭 clip 时仍存在静态资源契约

第一次修复裁剪蒙版的 constant buffer 后，普通测试和真机画面均正常，但开启 Metal API Validation 后，17 个测试报告 `solid_fs` 或 `gradient_fs` 缺少 sampler 绑定。原因是 shader 中声明的 clip texture 和 sampler 仍属于静态资源契约；运行时 uniform 关闭采样分支，并不代表 encoder 可以不绑定这些资源。

当前 backend 创建一个只读的 1×1 白色 coverage texture。没有活动 clip 时，Solid、Textured 和 Gradient 管线绑定该 texture；有 clip 时绑定真实蒙版。clip sampler 在两种情况下都绑定。白色 coverage 不改变输出，同时让每次 draw 都满足完整资源契约。

Metal 单元测试现默认开启 API Validation。以后新增 shader 参数但漏掉 encoder 绑定时，测试会直接失败，而不是等到某一代真机表现为黑屏。

后续公开 API 覆盖审计发现，原有测试仍未执行 Alpha8 更新、RGB/单通道导入、全部 sampler 寻址模式、`DstAtop` 混合和 swapchain 的完整 acquire/resize/present 合约。现已增加专门的 API contract 测试并补齐枚举分支；详细矩阵见 `METAL_API_VALIDATION.md`。

同一次审计还发现 backend finalize 后保留了旧的 last-readback texture。设备对象在后台恢复时重新初始化，旧 texture 可能被新 command queue 当作 presentation 输入。finalize 现重建空 Metal context，并通过“绘制 → finalize → initialize → last texture 必须为空”的回归测试锁定该行为。

纹理更新入口还存在格式边界不严的问题：RGBA update 曾能接收 Alpha8 resource，Alpha8 update 也能接收 RGBA resource 和零尺寸 region。后者可能把错误的 `bytesPerRow` 交给 Metal。两个入口现校验 backend 状态、resource 格式和正尺寸，错误调用在进入 `replaceRegion` 前返回。

## 问题六：窗口适配正确，但局部混合仍与 Android 不一致

最初各端直接按自身窗口重新排版。横屏宽高比变化时，卡片、文字、线宽和动画几何会分别变化，画面并不是整体等比放大。早期修复曾采用某台 Android 设备测得的 `786 x 377 / 393 x 759`，但该尺寸混入了设备 DPR 和系统栏。长期标准现改为严格旋转对称的逻辑内容窗口：横屏 `800 x 400`，竖屏 `400 x 800`。宿主先扣除 safe area，再把完整内容窗口等比缩放、水平居中并贴齐可用区域顶部；多余区域仅留作背景，不参与重排。旧尺寸只作为 `legacy_android` 历史回归档保留。

统一布局后，分区像素比较又发现 `SCREEN` 卡片仍有稳定差异。根因是 Metal shader 输出预乘颜色，而 OpenGL/Software 的既有 `SCREEN` 合约使用直颜色；原测试只覆盖不透明前景，alpha 为 1 时两种路径结果相同，因此没有暴露问题。Metal Solid 管线现在仅在 `SCREEN` 模式下保留直 RGB，其余模式继续使用预乘输出。新增半透明 blend 回归后，Blend 分区的 Metal/Software 差异从 mean `3.10874`、divergent `10.354%` 降为 mean `0.00194`、divergent `0%`。

跨设备截图中的 nearest-neighbor checkerboard 还会因 safe-area 后的非整数 viewport scale 和截图归一化出现边缘色差。Software 参考也必须显式使用 `DPR=3`，不能拿 1x dump 参与比较。该区域使用独立的 `image_sampling` profile：保持 mean delta 上限 `1.5`，只对物理像素相位造成的边缘颜色使用更高 channel threshold。这样不会放过纹理缺失、错误颜色、tile 尺寸或几何偏移。

## 同类问题审计

已按 Metal shader 的 `buffer`、`texture`、`sampler` 声明逐条核对 encoder：

| 管线 | 必需资源 | 结论 |
| --- | --- | --- |
| Solid | fragment buffer 0、texture 0、sampler 0 | 已绑定；无 clip 时使用白色 fallback texture |
| Textured / alpha texture | texture 0/1、sampler 0、fragment buffer 0 | 已绑定；无 clip 时 texture 1 使用 fallback |
| Gradient | texture 0、sampler 0、fragment buffer 0 | 已绑定；无 clip 时使用 fallback |
| Clip fill | texture 0、sampler 0、fragment buffer 0 | 已绑定 |
| Mask multiply | texture 0/1、sampler 0 | 已绑定 |
| Blur | texture 0、sampler 0、fragment buffer 0 | 已绑定 |
| Shadow compose | texture 0、sampler 0、fragment buffer 0 | 已绑定 |
| Inner-shadow compose | texture 0/1、sampler 0、fragment buffer 0 | 已绑定 |
| Present | texture 0 | 已绑定 |

完整 API Validation 暴露并修复了无 clip 时的静态 texture/sampler 漏绑；修复后全部 Metal 测试、iOS 模拟器和 A14 真机验证层运行均未再报告资源契约错误。另发现 mipmap 创建/更新、blur 和 inner-shadow 的同步命令缓冲区原先没有检查执行结果；这些路径现已在 `waitUntilCompleted` 后检查错误，避免真机 GPU 失败被静默转换成空白纹理。主绘制、裁剪和 presentation 同样保留阶段化错误输出。

初始化阶段也已改为强校验：10 条必需 pipeline 和默认 sampler 必须全部创建成功，backend 才会进入 ready 状态，避免单条 pipeline 初始化失败后只显示部分内容。

## 回归验证清单

每次修改 Metal shader、资源布局、CoreText upload 或展示逻辑后至少执行：

1. Metal 单元测试及 iOS parity 测试；Metal 测试默认开启 API Validation。
2. 模拟器 UI 测试：竖屏、横屏、进入后台、恢复前台、终止后冷启动。
3. 真机 Release 运行，检查内容、CoreText、emoji、裁剪、渐变和阴影。
4. 观察至少两个 FPS 统计窗口，目标为接近显示器刷新率的 60 fps。
5. 检查控制台中是否出现任一阶段的 `command buffer failed`。
6. 需要像素级诊断时才开启 `--capture-frames`，完成后关闭，以免读回影响性能判断。
