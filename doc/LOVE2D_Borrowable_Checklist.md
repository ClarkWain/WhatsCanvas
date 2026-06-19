# LÖVE 2D 引擎可借鉴点完成清单

> 来源：`.love2dgame` 引擎与 WhatsCanvas 当前架构对比分析
> 日期：2026-06-19
> 评分标准：1-5 分（5 = 强烈推荐，1 = 可选）
> 耗时估算：S = 半天，M = 1-3 天，L = 1 周+，XL = 2 周+

---

## 总览

| # | 改进项 | 分数 | 耗时 | 状态 |
|---|--------|------|------|------|
| 1 | Canvas/Texture 统一 | 5 | XL | ⬜ 未开始 |
| 2 | 流式批处理 (Batched Draw) | 5 | L | ⬜ 未开始 |
| 3 | 多后端渲染抽象 | 4 | XL | ⬜ 未开始 |
| 4 | 统一 DrawCommand 结构 | 5 | M | ⬜ 未开始 |
| 5 | GraphicsState 栈扩展 | 4 | S | ⬜ 未开始 |
| 6 | Shader validateDrawState | 4 | S | ⬜ 未开始 |
| 7 | StreamBuffer 顶点流 | 5 | M | ⬜ 未开始 |
| 8 | Lazy Render Pass | 3 | S | ⬜ 未开始 |
| 9 | Gamma 校正管线 | 3 | M | ⬜ 未开始 |
| 10 | 帧边界 present/reset | 4 | S | ⬜ 未开始 |
| 11 | Volatile 资源生命周期 | 4 | M | ⬜ 未开始 |
| 12 | 临时资源池 | 3 | M | ⬜ 未开始 |
| 13 | 异步 Readback + Fence Sync | 3 | M | ⬜ 未开始 |
| 14 | 全局 Quad/Fan Index Buffer | 4 | S | ⬜ 未开始 |
| 15 | Texel Buffer 抽象 | 2 | M | ⬜ 未开始 |
| 16 | DrawMode/ArcMode 组合枚举 | 3 | S | ⬜ 未开始 |
| 17 | Deprecation Warning 系统 | 2 | S | ⬜ 未开始 |
| 18 | isPixelFormatSupported 能力查询 | 4 | S | ⬜ 未开始 |
| 19 | SpriteBatch 批量绘制 | 3 | L | ⬜ 未开始 |
| 20 | 窗口 resize 无痛重建 | 4 | M | ⬜ 未开始 |

---

## 详细清单

### 1. Canvas/Texture 统一

- **分数**：5/5
- **原因**：当前 `Canvas` 和 `Image` 是完全独立的类型，`ImageResource` 和 `IRenderTarget` 也是两个接口，导致代码重复、功能割裂。统一后所有纹理操作（filter/wrap/mipmap/replacePixels/renderTo）自动对 Canvas 可用，SpriteBatch/Mesh 可直接把 Canvas 当纹理用。
- **LÖVE 的做法**：`newCanvas()` 本质上是 `newTexture({renderTarget=true})`。Canvas 不是独立类型，而是一种 Texture 配置。Texture 类有 `isRenderTarget()` 判断，render target handle 和 sampler handle 都在同一个对象上。
  - 参考：`src/modules/graphics/Texture.h` 第 181-193 行（Settings 结构体）
  - 参考：`src/modules/graphics/Graphics.cpp` 第 1346 行（getTemporaryTexture 中 `settings.renderTarget = true`）
- **WhatsCanvas 改进思路**：
  1. 将 `IRenderTarget` 能力合入 `ImageResource` 接口，增加 `isRenderTarget()` / `beginRenderTo()` / `endRenderTo()` 方法
  2. `Canvas` 类内部持有一个 `ImageResource`（renderTarget=true），而非独立的 FBO 管理
  3. `drawImage` 可以直接接受 Canvas 作为源纹理
- **耗时**：XL（2 周+）— 涉及公共 API 变更和全量重构

---

### 2. 流式批处理 (Batched Draw)

- **分数**：5/5
- **原因**：当前每次 `drawRect`/`drawLine`/`drawCircle` 都创建独立 Command 对象、分配顶点数据、绑定自己的 shader program。连续 100 个矩形会产生 100 次 draw call。批处理可将同类图元合并，显著降低 GPU 开销。
- **LÖVE 的做法**：`rectangle`/`circle`/`line` 不直接发 draw call，而是把顶点写入共享 StreamBuffer，累积后统一 flush。`requestBatchedDraw()` 判断 primitive/format/texture/shader 是否变化，变化时才 flush。`flushBatchedDraws()` 统一 unmap + 构造 DrawCommand + 提交后端。
  - 参考：`src/modules/graphics/Graphics.h` 第 995-1008 行（BatchedDrawState）
  - 参考：`src/modules/graphics/Graphics.cpp` 第 1956-2091 行（requestBatchedDraw）
  - 参考：`src/modules/graphics/Graphics.cpp` 第 2093-2186 行（flushBatchedDraws）
- **WhatsCanvas 改进思路**：
  1. 在 `Renderer` 中增加 `BatchedDrawState`，维护当前批次的 primitive mode / blend mode / texture / shader
  2. `Canvas::drawRect` 等方法不再直接 `submit(Command)`，而是调用 `renderer->requestBatchedDraw(...)` 获取顶点流指针
  3. 在 `flush()` 或状态变化时统一提交
- **耗时**：L（1 周+）— 需要重构 Command 提交链路

---

### 3. 多后端渲染抽象

- **分数**：4/5
- **原因**：当前仅 OpenGL 实现，`IRenderDevice` 接口已预留但无第二实现。未来若要支持 Metal（macOS/iOS）或 Vulkan（Windows/Android），需要完整的后端选择和资源子类体系。
- **LÖVE 的做法**：`Graphics` 基类定义纯虚接口（`draw`/`drawQuads`/`setRenderTargetsInternal` 等），三个子类各自实现。启动时按平台优先级尝试创建（Metal > Vulkan > OpenGL），用户可通过 `--renderers` 覆盖。窗口层（SDL）按 renderer 创建对应上下文。
  - 参考：`src/modules/graphics/Graphics.cpp` 第 109-188 行（createInstance + rendererOrder）
  - 参考：`src/modules/window/sdl/Window.cpp` 第 337-425 行（窗口与渲染器绑定）
- **WhatsCanvas 改进思路**：
  1. 当前 `IRenderDevice` 已是正确方向，补齐 `draw(DrawCommand)` / `setRenderTargets()` 等纯虚接口
  2. 每个后端有独立的 Texture/Buffer/Shader 子类
  3. 增加 `createInstance()` 工厂函数 + 平台条件编译 + 运行时降级
- **耗时**：XL（2 周+）— 每个后端需独立实现

---

### 4. 统一 DrawCommand 结构

- **分数**：5/5
- **原因**：当前有 5 种独立 Command 子类（DrawPoints/Lines/Path/Image/Text），每种 `execute()` 都重复 `applyBlendMode + applyClipState + getProgram()->draw()` 三步模板。统一后可消除 ~150 行重复代码，为批处理铺路，减少后端虚函数数量。
- **LÖVE 的做法**：所有绘制归结为两种结构体 `DrawCommand`（非索引）和 `DrawIndexedCommand`（索引），后端只需实现这两个函数即可处理所有图元类型。
  - 参考：`src/modules/graphics/Graphics.h` 第 235-304 行（DrawCommand / DrawIndexedCommand / BatchedDrawCommand）
  - 参考：`src/modules/graphics/opengl/Graphics.cpp` 第 556-610 行（draw 实现）
- **WhatsCanvas 改进思路**：
  1. 定义统一的 `DrawCommand` 结构体（primitiveType + vertexData + indexData + texture + blendMode + clipState）
  2. 各 `DrawXxxCommand` 子类改为填充 `DrawCommand` 数据，不再各自调 shader
  3. `Renderer::flush()` 统一遍历 `DrawCommand` 列表提交后端
- **耗时**：M（1-3 天）— 结构体设计 + 迁移现有 5 种 Command

---

### 5. GraphicsState 栈扩展

- **分数**：4/5
- **原因**：当前 `GraphicsStateStack` 只有矩阵栈，clip/blend/scissor 状态散落在 `RenderContext` 内部。切换 render target 或嵌套 push/pop 时状态不能自动恢复。
- **LÖVE 的做法**：`DisplayState` 包含 matrix、scissor、stencil、blend、shader、color、colorMask、winding、wireframe 等所有渲染状态。`push()` 保存完整快照，`pop()` 恢复。切换 render target 时自动 push/pop。
  - 参考：`src/modules/graphics/Graphics.h` 第 940-985 行（DisplayState）
  - 参考：`src/modules/graphics/Graphics.cpp` 第 823-930 行（状态管理）
- **WhatsCanvas 改进思路**：
  1. 扩展 `GraphicsState` 结构体，加入 blendMode、scissor、clipMask、color
  2. `GraphicsStateStack::push()` 保存完整状态快照
  3. `pop()` 恢复所有状态并标记 `RenderContext` 需要重新应用
- **耗时**：S（半天）— 结构体扩展 + 栈逻辑调整

---

### 6. Shader validateDrawState

- **分数**：4/5
- **原因**：当前如果用户传了错误类型的 Image 到 drawImage，或 blend mode 与 shader 不兼容，不会有运行时提示，直到出现 GL 错误或视觉异常。
- **LÖVE 的做法**：每次 draw 前调 `Shader::validateDrawState(primtype, maintex)`，检查 shader 是否期望点图元、main texture 类型是否匹配、像素格式 base type 兼容性、depth sampler 一致性。
  - 参考：`src/modules/graphics/Shader.cpp` 第 961-1010 行
  - 参考：`src/modules/graphics/Graphics.cpp` 第 2034-2037 行（在 requestBatchedDraw 中调用）
- **WhatsCanvas 改进思路**：
  1. 在 `GLProgram` 或 `DrawCommand` 提交前增加 `validateDrawState()` 方法
  2. 检查项：纹理格式与 shader sampler 类型匹配、blend mode 合法性、顶点数据非空
  3. 不匹配时抛出带上下文的异常（而非静默 GL 错误）
- **耗时**：S（半天）— 校验逻辑 + 错误消息

---

### 7. StreamBuffer 顶点流

- **分数**：5/5
- **原因**：当前 `DrawPathData`、`DrawPointsData` 等每帧创建新的 `std::vector<float>` 存顶点数据，然后在 `execute()` 中逐个上传。StreamBuffer 可避免每帧分配、减少 CPU-GPU 传输、实现 triple buffering 避免 stall。
- **LÖVE 的做法**：`StreamBuffer` 内部是 ring buffer，`map()` 返回可写指针直接写入，`unmap()` 返回偏移量。`BatchedDrawState` 维护两个顶点流（位置 + 颜色/texcoord）和一个索引流。
  - 参考：`src/modules/graphics/StreamBuffer.h`
  - 参考：`src/modules/graphics/Graphics.cpp` 第 1956-2091 行（map/unmap 流程）
- **WhatsCanvas 改进思路**：
  1. 实现 `GLStreamBuffer` 类，内部用 `glBufferSubData` 或 persistent mapping
  2. 替换 `DrawXxxData` 中的 `std::vector<float>` 为 StreamBuffer 偏移引用
  3. 配合批处理（改进项 #2）使用
- **耗时**：M（1-3 天）— StreamBuffer 实现 + 数据结构迁移

---

### 8. Lazy Render Pass

- **分数**：3/5
- **原因**：当前 `IRenderTarget::begin()` / `end()` 是立即执行的。lazy pass 可避免"设了 canvas 但什么都没画"时的空 render pass 开销，也为 Vulkan 后端迁移打基础。
- **LÖVE 的做法**：Vulkan 后端的 `setRenderTargetsInternal` 只构建 `renderPassState` 结构体，不发任何 GPU 命令。真正的 `vkCmdBeginRenderPass` 延迟到第一个 draw call 之前（`startRenderPass`）。
  - 参考：`src/modules/graphics/vulkan/Graphics.cpp` 第 1310-1325 行（setRenderTargetsInternal）
  - 参考：`src/modules/graphics/vulkan/Graphics.cpp` 第 2879-2900 行（startRenderPass）
- **WhatsCanvas 改进思路**：
  1. `IRenderTarget::begin()` 改为只记录"目标已设置"标记
  2. 第一个 draw command 执行时才真正 `glBindFramebuffer` + `glViewport`
  3. `end()` 时如果没有任何 draw 则跳过 FBO 绑定
- **耗时**：S（半天）— 标记位 + 延迟绑定逻辑

---

### 9. Gamma 校正管线

- **分数**：3/5
- **原因**：当前所有颜色计算都在 sRGB 空间进行，半透明混合结果在视觉上不正确（例如两个半透明红色叠加会偏暗）。
- **LÖVE 的做法**：`love.conf` 中设置 `gammacorrect=true` 后，所有颜色值在输入时自动 sRGB→linear 转换，canvas 内部使用 sRGB 格式存储，shader 中的混合在 linear 空间进行。`glEnable(GL_FRAMEBUFFER_SRGB)` 控制 FBO 的 sRGB 写入。
  - 参考：`src/modules/graphics/Graphics.cpp` 第 42-68 行（gammaCorrectColor）
  - 参考：`src/modules/graphics/opengl/Graphics.cpp` 第 333 行（ENABLE_FRAMEBUFFER_SRGB）
- **WhatsCanvas 改进思路**：
  1. 增加 `Canvas::setGammaCorrect(bool)` 全局开关
  2. 颜色输入时做 sRGB→linear 转换
  3. FBO 创建时使用 `GL_SRGB8_ALPHA8` 内部格式
  4. shader 中混合在 linear 空间，输出时硬件自动转回 sRGB
- **耗时**：M（1-3 天）— 颜色转换 + FBO 格式 + shader 适配

---

### 10. 帧边界 present/reset

- **分数**：4/5
- **原因**：当前没有帧边界概念，渲染状态（blend、scissor、clip）在多次 flush 之间不会自动清理。容易产生状态泄漏 bug。
- **LÖVE 的做法**：`present()` 在帧结束时做完整重置：flush 所有 batched draws → resolve MSAA → auto-generate mipmaps → 切换到默认 backbuffer → screenshot 回调 → 重置帧计数器。
  - 参考：`src/modules/graphics/opengl/Graphics.cpp` 第 1169-1292 行（present 完整实现）
  - 参考：`src/modules/graphics/Graphics.cpp` 第 2730-2760 行（getStats / 帧统计）
- **WhatsCanvas 改进思路**：
  1. 增加 `Canvas::present()` 或 `Renderer::endFrame()` 方法
  2. 帧结束时：flush 所有命令 → 重置 blend 为 SrcOver → 关闭 scissor → 清空 clip 栈 → 重置当前 shader
  3. 统计 drawCalls / renderTargetSwitches 供调试
- **耗时**：S（半天）— 帧结束清理逻辑

---

### 11. Volatile 资源生命周期

- **分数**：4/5
- **原因**：当前 `Canvas`、`Image`、`OpenGLRenderDevice` 各自管理 GL 资源生命周期，没有统一机制处理上下文丢失。移动端后台恢复或窗口重建时会导致 GL 资源全部失效。
- **LÖVE 的做法**：所有 GPU 资源继承 `Volatile` 基类，维护全局链表。窗口模式切换或上下文丢失时，自动调用 `unloadVolatile()` 释放，再调 `loadVolatile()` 重建。
  - 参考：`src/modules/graphics/Volatile.h`（Volatile 基类）
  - 参考：`src/modules/graphics/Volatile.cpp`（loadAll / unloadAll）
- **WhatsCanvas 改进思路**：
  1. 定义 `IVolatile` 接口（`loadVolatile()` / `unloadVolatile()`）
  2. `ImageResource`、`ClipMaskResource`、`GLProgram` 继承此接口
  3. `OpenGLRenderDevice` 维护 Volatile 列表，在 `initializeBackend()` 时 `loadAll()`，`finalizeBackend()` 时 `unloadAll()`
- **耗时**：M（1-3 天）— 接口定义 + 所有 GL 资源类迁移

---

### 12. 临时资源池

- **分数**：3/5
- **原因**：当前每次 `renderCommandsToImageResource()` 都创建新的 `IRenderTarget`（FBO+texture），用完即销毁。频繁 offscreen rendering（clip mask 生成、多层合成）会产生大量 GL 对象分配/释放。
- **LÖVE 的做法**：维护 `temporaryTextures` 池，`getTemporaryTexture(format, w, h, msaa)` 先从池中找匹配的，找不到才新建。`updateTemporaryResources()` 每帧清理超过 2 帧未使用的临时资源。
  - 参考：`src/modules/graphics/Graphics.h` 第 1029-1059 行（TemporaryTexture 结构）
  - 参考：`src/modules/graphics/Graphics.cpp` 第 1324-1370 行（getTemporaryTexture）
  - 参考：`src/modules/graphics/Graphics.cpp` 第 1415-1444 行（updateTemporaryResources）
- **WhatsCanvas 改进思路**：
  1. `OpenGLRenderDevice` 维护 `std::vector<IRenderTarget*>` 临时目标池
  2. `createRenderTarget()` 改为 `getTemporaryRenderTarget(w, h)`，先查池
  3. 帧结束时清理超过 N 帧未使用的临时目标
- **耗时**：M（1-3 天）— 池管理 + 匹配逻辑

---

### 13. 异步 Readback + Fence Sync

- **分数**：3/5
- **原因**：当前 `readPixelsRGBA()` 是完全同步的（`glReadPixels` 阻塞 CPU 等待 GPU）。高分辨率或复杂场景下，这一帧的 GPU 工作会完全被 CPU 等待浪费。
- **LÖVE 的做法**：`GraphicsReadback` 支持异步像素回读，提交后不阻塞，下一帧通过 GL fence 同步检查完成状态，完成后触发回调。`pendingReadbacks` 列表在每帧 `present()` 时检查。
  - 参考：`src/modules/graphics/GraphicsReadback.h`
  - 参考：`src/modules/graphics/opengl/GraphicsReadback.cpp`（FenceSync 实现）
- **WhatsCanvas 改进思路**：
  1. 增加 `requestReadbackAsync(callback)` 接口
  2. 使用 `glFenceSync` 提交 fence，PBO 做异步传输
  3. 下一帧 `present()` 时检查 fence 完成状态，触发回调
- **耗时**：M（1-3 天）— PBO + fence + 回调机制

---

### 14. 全局 Quad/Fan Index Buffer

- **分数**：4/5
- **原因**：当前 `DrawPathData` 中的 `std::vector<float> points` 每帧重建，且没有共享的索引缓冲。对于矩形和扇形图元，固定索引模式可完全复用。
- **LÖVE 的做法**：初始化时预生成两张静态索引表（覆盖 uint16 最大顶点数），所有 quad/fan 绘制复用。`quadIndexBuffer` 和 `fanIndexBuffer` 设为 immutable。
  - 参考：`src/modules/graphics/Graphics.cpp` 第 265-310 行（createQuadIndexBuffer / createFanIndexBuffer）
- **WhatsCanvas 改进思路**：
  1. `OpenGLRenderDevice` 初始化时预生成 quad 索引表 `[0,1,2,0,2,3,4,5,6,4,6,7,...]`
  2. `drawRect` 等矩形图元只上传 4 个顶点，复用全局索引
  3. 扇形图元（circle/arc fill）复用 fan 索引表
- **耗时**：S（半天）— 索引表生成 + 绑定逻辑

---

### 15. Texel Buffer 抽象

- **分数**：2/5
- **原因**：当前字体渲染（stb_easy_font）是 CPU 端逐顶点展开。如果要升级到 GPU 文本渲染，texel buffer 可高效存储 glyph 查询表。
- **LÖVE 的做法**：支持 `BUFFERUSAGE_TEXEL`，把 Buffer 作为纹理通过 `samplerBuffer` 在 shader 中按索引随机访问。
  - 参考：`src/modules/graphics/vertex.h` 第 80 行
- **WhatsCanvas 改进思路**：
  1. `IRenderDevice` 增加 `createTexelBuffer(size)` 接口
  2. 字体后端用 texel buffer 存储 glyph atlas 索引
  3. shader 中通过 `samplerBuffer` 查询 glyph 数据
- **耗时**：M（1-3 天）— 需配合 GPU 文本渲染改造

---

### 16. DrawMode/ArcMode 组合枚举

- **分数**：3/5
- **原因**：当前 `Paint::Style` 只有 `FILL / STROKE / FILL_AND_STROKE`，`drawArc` 的 open/close/pie 语义没有对应枚举。统一的 DrawMode + ArcMode 组合让图元生成代码可共享，减少 `Canvas.cpp` 中 ~300 行重复点生成代码。
- **LÖVE 的做法**：用两层枚举表达绘制意图：`DrawMode { DRAW_LINE, DRAW_FILL }` + `ArcMode { ARC_OPEN, ARC_CLOSED, ARC_PIE }`。`rectangle` 内部统一转成 `polygon(DRAW_FILL, 5个点)`，圆角矩形转成带弧度插值的 polygon，完全复用同一套三角化+索引逻辑。
  - 参考：`src/modules/graphics/Graphics.h` 第 121-160 行
  - 参考：`src/modules/graphics/Graphics.cpp` 第 2460-2649 行（rectangle/circle/ellipse/arc 全部转 polygon）
- **WhatsCanvas 改进思路**：
  1. 增加 `DrawMode { Fill, Stroke }` 和 `ArcMode { Open, Closed, Pie }` 枚举
  2. `drawRect`/`drawCircle`/`drawArc` 内部统一转成 `drawPolygon(mode, points)`
  3. 消除 `Canvas.cpp` 中重复的点生成代码
- **耗时**：S（半天）— 枚举定义 + 图元生成重构

---

### 17. Deprecation Warning 系统

- **分数**：2/5
- **原因**：如果未来 API 演进（如 `Canvas::setSize` 被 `Canvas::resize` 替代），需要机制在不破坏旧代码前提下通知用户。
- **LÖVE 的做法**：`Deprecations` 类在 present 时统一绘制弃用警告图标，并跟踪已触发的弃用项避免重复。
  - 参考：`src/modules/graphics/Deprecations.h` + `Deprecations.cpp`
- **WhatsCanvas 改进思路**：
  1. 增加 `DeprecationTracker` 单例
  2. 弃用 API 调用时记录（带文件名/行号）
  3. 日志输出 + 可选的屏幕角标提示
- **耗时**：S（半天）— 跟踪器 + 日志集成

---

### 18. isPixelFormatSupported 能力查询

- **分数**：4/5
- **原因**：当前 `OpenGLRenderDevice` 不暴露任何格式查询。如果要支持 HDR canvas、浮点格式、compute shader 可写纹理，需要能力查询机制来避免运行时失败。
- **LÖVE 的做法**：`isPixelFormatSupported(format, usageFlags)` 接口，后端初始化时查询 GL 扩展并缓存。`usage` 可以是 `SAMPLE / LINEAR / RENDERTARGET / BLEND / MSAA / COMPUTEWRITE` 组合。上层代码据此自动降级。
  - 参考：`src/modules/graphics/Graphics.h` 第 859 行
  - 参考：`src/modules/graphics/Graphics.cpp` 第 1149-1177 行（临时 depth/stencil 自动降级链）
- **WhatsCanvas 改进思路**：
  1. `IRenderDevice` 增加 `isPixelFormatSupported(format, usage)` 纯虚函数
  2. `OpenGLRenderDevice` 初始化时查询 `GL_INTERNALFORMAT_SUPPORTED` 等扩展
  3. `Canvas` 创建时自动降级到支持的格式
- **耗时**：S（半天）— 查询接口 + 降级逻辑

---

### 19. SpriteBatch 批量绘制

- **分数**：3/5
- **原因**：如果 WhatsCanvas 未来要做游戏/UI 框架（而非纯 2D canvas API），SpriteBatch 可在一次 draw call 中渲染数千个同纹理精灵。当前"每个图元一个 Command + 一次 draw"架构做不到。
- **LÖVE 的做法**：`SpriteBatch` 把大量同纹理精灵合并到一个 VBO，只发一次 draw call。内部维护"活跃精灵数"计数器和"脏标记"，只在 `flush()` 时上传变化部分。
  - 参考：`src/modules/graphics/SpriteBatch.h` + `SpriteBatch.cpp`
- **WhatsCanvas 改进思路**：
  1. 增加 `SpriteBatch` 类，绑定一个 `ImageResource`
  2. `add(x, y, quad)` 追加到内部 VBO，不触发 GPU 上传
  3. `flush()` 一次性上传 + draw call
  4. 依赖批处理（改进项 #2）和 StreamBuffer（改进项 #7）
- **耗时**：L（1 周+）— 需批处理基础设施先行

---

### 20. 窗口 resize 无痛重建

- **分数**：4/5
- **原因**：当前 `Canvas::setSize()` 会销毁并重建整个 FBO 和 texture。如果 canvas 是用户创建的 offscreen target，resize 会丢失内容。
- **LÖVE 的做法**：区分"系统 backbuffer"和"用户 canvas"。`backbufferChanged()` 只重建内部 backbuffer（internalBackbuffer + internalBackbufferDepthStencil），用户创建的 Texture/Canvas 完全不受影响。`love.window.setMode` 也被改成不清理 canvas 内容。
  - 参考：`src/modules/graphics/opengl/Graphics.cpp` 第 155-257 行（backbufferChanged）
- **WhatsCanvas 改进思路**：
  1. 区分"系统 backbuffer"（窗口大小变化时重建）和"用户 offscreen target"（不受窗口影响）
  2. `Canvas::setSize()` 只影响该 Canvas 自己的 FBO，不波及其他资源
  3. 窗口 resize 事件只触发系统 backbuffer 重建
- **耗时**：M（1-3 天）— backbuffer 分离 + resize 事件路由

---

## 推荐实施顺序

### 阶段一：基础设施（1-2 周）
优先做不改变公共 API、但为后续铺路的改进：

1. **#4 统一 DrawCommand** — 消除重复，为批处理铺路
2. **#5 GraphicsState 栈扩展** — 正确的 push/pop 语义
3. **#6 Shader validateDrawState** — 调试体验提升
4. **#10 帧边界 present/reset** — 状态泄漏防护
5. **#14 全局 Quad/Fan Index Buffer** — 快速性能提升
6. **#16 DrawMode/ArcMode 组合枚举** — 代码简化

### 阶段二：性能核心（2-3 周）
依赖阶段一的基础设施：

7. **#7 StreamBuffer 顶点流** — 配合批处理
8. **#2 流式批处理** — 最大性能收益
9. **#8 Lazy Render Pass** — 减少空 pass
10. **#11 Volatile 资源生命周期** — 健壮性
11. **#20 窗口 resize 无痛重建** — 用户体验

### 阶段三：能力扩展（2-4 周）
在稳定基础上扩展功能：

12. **#18 isPixelFormatSupported** — 格式能力查询
13. **#12 临时资源池** — offscreen 性能
14. **#13 异步 Readback** — 读回性能
15. **#9 Gamma 校正管线** — 视觉正确性
16. **#1 Canvas/Texture 统一** — API 简化
17. **#19 SpriteBatch** — 批量精灵

### 阶段四：远期目标（4 周+）
18. **#3 多后端渲染抽象** — Metal/Vulkan
19. **#15 Texel Buffer** — GPU 文本
20. **#17 Deprecation Warning** — API 演进

---

## 依赖关系

```
#4 统一 DrawCommand ──┬──> #2 流式批处理 ──> #19 SpriteBatch
                      │
#7 StreamBuffer ──────┘
                      
#5 State 栈 ──> #10 帧边界 ──> #11 Volatile ──> #20 resize 无痛

#18 格式查询 ──> #1 Canvas/Texture 统一 ──> #3 多后端

#16 DrawMode 枚举 ──> #14 全局 Index Buffer（独立但受益）
```
