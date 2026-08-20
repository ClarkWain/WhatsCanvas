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

## 同类问题审计

已按 Metal shader 的 `buffer`、`texture`、`sampler` 声明逐条核对 encoder：

| 管线 | 必需资源 | 结论 |
| --- | --- | --- |
| Solid | fragment buffer 0 | 普通绘制已绑定；裁剪蒙版缺失已修复 |
| Textured / alpha texture | texture 0、sampler 0、fragment buffer 0 | 已绑定；clip texture 1 仅在 clip-enabled 时使用 |
| Gradient | texture 0、sampler 0、fragment buffer 0 | 已绑定；clip 资源受开关保护 |
| Clip fill | texture 0、sampler 0、fragment buffer 0 | 已绑定 |
| Mask multiply | texture 0/1、sampler 0 | 已绑定 |
| Blur | texture 0、sampler 0、fragment buffer 0 | 已绑定 |
| Shadow compose | texture 0、sampler 0、fragment buffer 0 | 已绑定 |
| Inner-shadow compose | texture 0/1、sampler 0、fragment buffer 0 | 已绑定 |
| Present | texture 0 | 已绑定 |

没有发现第二处与裁剪蒙版相同的必需资源漏绑。另发现 mipmap 创建/更新、blur 和 inner-shadow 的同步命令缓冲区原先没有检查执行结果；这些路径现已在 `waitUntilCompleted` 后检查错误，避免真机 GPU 失败被静默转换成空白纹理。主绘制、裁剪和 presentation 同样保留阶段化错误输出。

## 回归验证清单

每次修改 Metal shader、资源布局、CoreText upload 或展示逻辑后至少执行：

1. Metal 单元测试及 iOS parity 测试。
2. 模拟器 UI 测试：竖屏、横屏、进入后台、恢复前台、终止后冷启动。
3. 真机 Release 运行，检查内容、CoreText、emoji、裁剪、渐变和阴影。
4. 观察至少两个 FPS 统计窗口，目标为接近显示器刷新率的 60 fps。
5. 检查控制台中是否出现任一阶段的 `command buffer failed`。
6. 需要像素级诊断时才开启 `--capture-frames`，完成后关闭，以免读回影响性能判断。
