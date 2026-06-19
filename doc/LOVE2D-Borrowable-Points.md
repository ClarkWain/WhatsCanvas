# LÖVE 可借鉴点完成清单

> 基于对 `.love2dgame/` (LÖVE 12.x) 源码的深度分析，
> 对比 WhatsCanvas 当前架构，列出可借鉴的改进项。
>
> 生成日期：2026-06-19

---

## 评分说明

- **影响分 (Impact)**：对 WhatsCanvas 整体架构/性能/可维护性的提升程度，1-10
- **难度分 (Difficulty)**：实施所需的工作量和风险，1-10（越高越难）
- **优先级 (Priority)**：Impact / Difficulty 的比值，越大越值得优先做

---

## 一览表

| # | 改进项 | Impact | Difficulty | Priority | 状态 |
|---|--------|--------|------------|----------|------|
| 01 | 统一 DrawCommand 结构 | 9 | 4 | 2.25 | 待开始 |
| 02 | 流式批处理系统 | 10 | 8 | 1.25 | 待开始 |
| 03 | 多后端渲染抽象完善 | 8 | 9 | 0.89 | 待开始 |
| 04 | Canvas/Texture 统一为一个类 | 8 | 9 | 0.89 | 待开始 |
| 05 | GraphicsState 栈扩展 | 7 | 3 | 2.33 | 待开始 |
| 06 | Lazy Render Pass | 6 | 4 | 1.50 | 待开始 |
| 07 | Shader validateDrawState | 6 | 2 | 3.00 | 待开始 |
| 08 | StreamBuffer 顶点流 | 9 | 7 | 1.29 | 待开始 |
| 09 | Gamma 校正管线 | 7 | 6 | 1.17 | 待开始 |
| 10 | 帧边界 present/reset | 6 | 3 | 2.00 | 待开始 |
| 11 | Volatile 资源生命周期 | 7 | 5 | 1.40 | 待开始 |
| 12 | 临时资源池 | 6 | 4 | 1.50 | 待开始 |
| 13 | 异步 Readback + Fence Sync | 7 | 7 | 1.00 | 待开始 |
| 14 | 全局 Quad/Fan Index Buffer | 5 | 2 | 2.50 | 待开始 |
| 15 | Texel Buffer 抽象 | 5 | 8 | 0.63 | 待开始 |
| 16 | DrawMode/ArcMode 组合枚举 | 6 | 2 | 3.00 | 待开始 |
| 17 | Deprecation Warning 系统 | 4 | 2 | 2.00 | 待开始 |
| 18 | isPixelFormatSupported 查询 | 6 | 3 | 2.00 | 待开始 |
| 19 | SpriteBatch 批量绘制 | 8 | 6 | 1.33 | 待开始 |
| 20 | 窗口 resize 无痛重建 | 6 | 4 | 1.50 | 待开始 |

---

## 01. 统一 DrawCommand 结构

**优先级：2.25 (Impact 9 / Difficulty 4)**

### 当前问题

WhatsCanvas 有 5 种独立的 Command 子类，每种都重复 `applyBlendMode + applyClipState + getProgram()->draw()` 三步模板：

```cpp
// Commands.h — 每种 Command 的 execute() 几乎相同
class DrawPointsCommand : public Command {
    void execute(RenderContext &context) override {
        context.applyBlendMode(data_.blendMode);
        context.applyClipState(data_.scissor, data_.clipMask);
        DrawPointsProgram::getInstance()->draw(context, data_);
    }
};
// DrawLinesCommand、DrawPathCommand、DrawImageCommand、DrawTextCommand 同理
```

约 150 行重复代码，且无法合并同类图元。

### LÖVE 做法

所有绘制归结为两个结构体：

```cpp
// Graphics.h:235
struct DrawCommand {
    PrimitiveType primitiveType;
    VertexAttributesID attributesID;
    const BufferBindings *buffers;
    int vertexStart, vertexCount, instanceCount;
    Texture *texture;
    CullMode cullMode;
};

struct DrawIndexedCommand {
    // 同上 + indexCount, indexType, indexBuffer, indexBufferOffset
};
```

后端只需实现 `draw(DrawCommand)` 和 `draw(DrawIndexedCommand)` 两个函数。

### 改进思路

1. 定义 `DrawCommand` 通用结构体，包含 primitiveType、vertexRange、texture、blendMode、clipState
2. 将 `DrawPointsData` / `DrawLinesData` / `DrawPathData` / `DrawImageData` 统一转换为顶点流 + DrawCommand
3. 后端只实现 `draw(DrawCommand)` 一个虚函数
4. 为后续批处理铺平道路（同结构体可合并）

### 参考源码

- `src/modules/graphics/Graphics.h` 第 235-304 行
- `src/modules/graphics/opengl/Graphics.cpp` 第 556-610 行

---

## 02. 流式批处理系统

**优先级：1.25 (Impact 10 / Difficulty 8)**

### 当前问题

每次 `drawRect`/`drawLine`/`drawCircle` 都创建独立 Command + 独立顶点数据 + 独立 draw call。连续 100 个矩形产生 100 次 draw call。

### LÖVE 做法

`requestBatchedDraw()` 把顶点数据追加到共享 StreamBuffer，`flushBatchedDraws()` 时统一提交：

```
rectangle → polygon → requestBatchedDraw (map + 写顶点)
rectangle → polygon → requestBatchedDraw (追加到同一批次)
flushBatchedDraws → unmap + 构造 DrawCommand + draw(DrawIndexedCommand)
```

100 个矩形可能只产生 1-2 次 draw call。

### 改进思路

1. 先实现 #1 统一 DrawCommand（前置条件）
2. 引入 StreamBuffer（见 #8），提供 map/unmap 接口
3. 在 Canvas 层维护 `BatchedDrawState`，判断当前批次是否可合并（primitiveType + texture + blendMode 相同则合并）
4. 在 flush 时一次性提交所有累积顶点
5. 分阶段实施：先支持三角形批处理，再扩展到线条和路径

### 参考源码

- `src/modules/graphics/Graphics.h` 第 995-1008 行 — BatchedDrawState
- `src/modules/graphics/Graphics.cpp` 第 1956-2091 行 — requestBatchedDraw
- `src/modules/graphics/Graphics.cpp` 第 2093-2186 行 — flushBatchedDraws

---

## 03. 多后端渲染抽象完善

**优先级：0.89 (Impact 8 / Difficulty 9)**

### 当前问题

`IRenderDevice` 接口已定义（ADR-002），但只有 `OpenGLRenderDevice` 一个实现。未来要支持 Metal（macOS/iOS）或 Vulkan（跨平台），需要完善的后端抽象。

### LÖVE 做法

`Graphics` 基类定义纯虚接口，三个子类各自实现：

```cpp
// Graphics.cpp:165 — 按优先级尝试创建
for (auto r : rendererOrder) {
    if (r == RENDERER_VULKAN)  instance = vulkan::createInstance();
    if (r == RENDERER_OPENGL)  instance = opengl::createInstance();
    if (r == RENDERER_METAL)   instance = metal::createInstance();
    if (instance != nullptr) break;
}
```

每个后端有独立的 Texture/Buffer/Shader 子类（`opengl::Texture`、`vulkan::Texture` 等）。

### 改进思路

1. 先完善 `IRenderDevice` 接口，确保它覆盖所有后端需要的操作（当前缺少 compute dispatch、texture copy 等）
2. 将 `ImageResource` 拆分为更细粒度的后端资源接口（`ITextureResource`、`IFramebufferResource`）
3. 实现 `VulkanRenderDevice` 作为第二个后端，验证抽象的完整性
4. 添加后端选择机制（编译时宏 + 运行时降级）

### 参考源码

- `src/modules/graphics/Graphics.cpp` 第 109-188 行 — 后端选择
- `src/modules/graphics/opengl/Graphics.h` — OpenGL 子类
- `src/modules/graphics/vulkan/Graphics.h` — Vulkan 子类
- `src/modules/window/sdl/Window.cpp` 第 337-425 行 — 窗口与渲染器绑定

---

## 04. Canvas/Texture 统一为一个类

**优先级：0.89 (Impact 8 / Difficulty 9)**

### 当前问题

`Canvas` 和 `Image` 是完全独立的类型，`ImageResource` 和 `IRenderTarget` 也是两个接口。Canvas 不能直接当纹理用于其他绘制操作，需要额外的 `getImageResource()` 转换。

### LÖVE 做法

`newCanvas()` 本质就是 `newTexture({renderTarget=true})`。Canvas 不是独立类型，而是 Texture 的一种配置：

```cpp
// Texture.h:181
struct Settings {
    int width, height, layers;
    TextureType type;
    PixelFormat format;
    bool renderTarget = false;  // 关键 flag
    int msaa = 1;
    // ...
};
```

所有纹理操作（filter/wrap/mipmap/replacePixels）自动对 Canvas 可用。SpriteBatch/Mesh 可以直接把 Canvas 当纹理使用。

### 改进思路

1. 将 `renderTarget` 能力合入 `ImageResource` 或创建统一的 `ITextureResource` 接口
2. `Canvas` 变为 `ITextureResource` 的一个工厂方法（`createRenderTarget = true`）
3. 统一后的纹理接口自动支持 filter/wrap/mipmap/query
4. 这是一个大重构，建议在 #3（多后端抽象）之后做

### 参考源码

- `src/modules/graphics/Texture.h` 第 181-193 行
- `src/modules/graphics/Texture.cpp` 第 423 行
- `src/modules/graphics/Graphics.cpp` 第 1346 行

---

## 05. GraphicsState 栈扩展

**优先级：2.33 (Impact 7 / Difficulty 3)**

### 当前问题

`GraphicsStateStack` 只保存变换矩阵。`save()`/`restore()` 不会恢复 blend mode、scissor、颜色等状态。

### LÖVE 做法

`DisplayState` 保存完整的渲染状态快照：

```cpp
// Graphics.h:963
struct DisplayState {
    Colorf color;
    bool activeDepthTest = false;
    BlendState blend = computeBlendState(BLEND_ALPHA, ...);
    StencilState stencil;
    Winding winding;
    CullMode meshCullMode;
    StrongRef<Shader> shader;
    StrongRef<Texture> texture;
    StrongRef<Font> font;
    // ... 20+ 字段
};
```

`pushTransform()` / `popTransform()` 保存/恢复整个 DisplayState。

### 改进思路

1. 扩展 `GraphicsState` 结构，增加 `blendMode`、`scissor`、`color`、`clipMask` 等字段
2. `GraphicsStateStack::save()` 快照所有状态
3. `GraphicsStateStack::restore()` 恢复所有状态
4. 工作量小，收益明确

### 参考源码

- `src/modules/graphics/Graphics.h` 第 963-982 行
- `src/modules/graphics/Graphics.cpp` 第 823-930 行

---

## 06. Lazy Render Pass

**优先级：1.50 (Impact 6 / Difficulty 4)**

### 当前问题

`IRenderTarget::begin()` 立即绑定 FBO。如果设了 canvas 但什么都没画，会产生空 render pass 开销。

### LÖVE 做法

`setRenderTargetsInternal` 只构建状态结构体，不发 GPU 命令。真正的 `vkCmdBeginRenderPass` 延迟到第一个 draw call 之前：

```cpp
// vulkan/Graphics.cpp:1310 — 只构建状态
void Graphics::setRenderTargetsInternal(...) {
    if (renderPassState.active) endRenderPass();
    if (isWindow) setDefaultRenderPass();
    else setRenderPass(rts, pixelw, pixelh);
}

// vulkan/Graphics.cpp:2879 — 真正提交
void Graphics::startRenderPass() {
    vkCmdBeginRenderPass(commandBuffers.at(currentFrame), ...);
}
```

### 改进思路

1. `IRenderTarget::begin()` 改为惰性：只记录配置，不立即绑定
2. 在第一个 draw call 时才真正 `glBindFramebuffer`
3. 添加 `IRenderTarget::isActive()` 查询
4. 为 Vulkan 后端迁移打基础

### 参考源码

- `src/modules/graphics/vulkan/Graphics.cpp` 第 1310-1325 行
- `src/modules/graphics/vulkan/Graphics.cpp` 第 2879-2900 行

---

## 07. Shader validateDrawState

**优先级：3.00 (Impact 6 / Difficulty 2)**

### 当前问题

如果传了错误类型的 Image 到 drawImage，或 blend mode 与当前 shader 不兼容，没有运行时提示，直到出现 GL 错误或视觉异常。

### LÖVE 做法

每次 draw 前做类型一致性检查：

```cpp
// Shader.cpp:961
void Shader::validateDrawState(PrimitiveType primtype, Texture *maintex) const {
    // 检查 shader 是否期望点图元
    // 检查 main texture 类型是否匹配
    // 检查像素格式 base type 兼容性
    // 检查 depth sampler 一致性
}
```

### 改进思路

1. 在 `DrawPathProgram::draw()` / `DrawImageProgram::draw()` 入口添加参数校验
2. 检查 texture 类型与 shader 期望是否匹配
3. 检查 blend mode 与当前 state 是否一致
4. 抛出有意义的错误信息而非 GL 错误
5. 工作量极小，调试体验大幅提升

### 参考源码

- `src/modules/graphics/Shader.cpp` 第 961-1010 行
- `src/modules/graphics/Graphics.cpp` 第 2034-2037 行

---

## 08. StreamBuffer 顶点流

**优先级：1.29 (Impact 9 / Difficulty 7)**

### 当前问题

每次 draw 都创建新的 `std::vector<float>` 存顶点数据，然后在 `execute()` 中逐帧重新上传。大量小分配 + 频繁 `glBufferSubData`。

### LÖVE 做法

StreamBuffer 是 ring buffer，map 后返回指针直接写入，无需 `glBufferSubData`：

```cpp
// BatchedDrawState 中:
StreamBuffer *vb[2];        // 顶点流（最多 2 个 stream）
StreamBuffer *indexBuffer;  // 索引流
StreamBuffer::MapInfo vbMap[2];
```

支持 triple buffering 避免 CPU-GPU stall。

### 改进思路

1. 实现 `StreamBuffer` 类，内部维护 ring buffer（按帧偏移）
2. `map(size)` 返回可写指针，`unmap()` 提交本帧数据
3. 替换当前 `DrawPathData::points` 等动态数组
4. 分帧偏移避免 fence stall
5. 前置条件：#1（统一 DrawCommand）

### 参考源码

- `src/modules/graphics/StreamBuffer.h`
- `src/modules/graphics/Graphics.cpp` 第 1956-2091 行

---

## 09. Gamma 校正管线

**优先级：1.17 (Impact 7 / Difficulty 6)**

### 当前问题

所有颜色计算在 sRGB 空间进行，半透明混合结果视觉不正确（两个半透明红色叠加偏暗）。

### LÖVE 做法

全局开关 `gammacorrect=true` 后：
- 输入颜色自动 sRGB -> linear 转换
- Canvas 内部使用 sRGB 格式存储
- 混合在 linear 空间进行
- OpenGL 启用 `GL_FRAMEBUFFER_SRGB`

### 改进思路

1. 添加全局 `gammaCorrect` 开关
2. 在 `Canvas::setColor()` / `Paint` 设置颜色时做 sRGB -> linear 转换
3. 创建 Canvas/纹理时请求 sRGB 格式（`GL_SRGB8_ALPHA8`）
4. OpenGL 后端启用 `GL_FRAMEBUFFER_SRGB`
5. 需要逐 API 检查颜色入口

### 参考源码

- `src/modules/graphics/Graphics.cpp` 第 42-68 行
- `src/modules/graphics/opengl/Graphics.cpp` 第 333 行、第 784-785 行

---

## 10. 帧边界 present/reset

**优先级：2.00 (Impact 6 / Difficulty 3)**

### 当前问题

没有帧边界概念。渲染状态（blend、scissor、clip）在多次 flush 之间不会自动清理。

### LÖVE 做法

`present()` 在帧结束时做完整状态重置：
1. flush 所有 batched draws
2. resolve 所有 MSAA canvas
3. auto-generate mipmaps
4. 切换到默认 backbuffer FBO
5. screenshot 回调
6. 重置帧计数器

### 改进思路

1. 在渲染循环末尾添加 `Canvas::presentFrame()` 方法
2. 内部调用 `flushBatchedDraws()` + `resolveMSAA()` + `generateMipmaps()`
3. 重置帧级统计（drawCalls、renderTargetSwitches）
4. 工作量小，为批处理和 MSAA 打基础

### 参考源码

- `src/modules/graphics/opengl/Graphics.cpp` 第 1169-1292 行
- `src/modules/graphics/Graphics.cpp` 第 2730-2760 行

---

## 11. Volatile 资源生命周期

**优先级：1.40 (Impact 7 / Difficulty 5)**

### 当前问题

`Canvas`、`Image`、`OpenGLRenderDevice` 各自管理 GL 资源，没有统一机制处理上下文丢失。

### LÖVE 做法

所有 GPU 资源继承 `Volatile` 基类，维护全局链表。上下文切换时自动卸载/重建：

```cpp
// Volatile.h:39
class Volatile {
    static std::list<Volatile *> all;
    virtual bool loadVolatile() = 0;
    virtual void unloadVolatile() = 0;
};
```

### 改进思路

1. 创建 `IGPUResource` 接口，定义 `loadGPU()` / `unloadGPU()` 虚函数
2. `Canvas`、`Image`、`GLProgram`、`GLVertexArray` 等继承该接口
3. `OpenGLRenderDevice` 维护资源链表，`finalizeBackend()` 时遍历卸载
4. 为移动端后台恢复和窗口重建做准备

### 参考源码

- `src/modules/graphics/Volatile.h` + `Volatile.cpp`

---

## 12. 临时资源池

**优先级：1.50 (Impact 6 / Difficulty 4)**

### 当前问题

每次 `renderCommandsToImageResource()` 都创建新的 `IRenderTarget`（FBO+texture），用完即销毁。频繁 offscreen rendering 产生大量 GL 对象分配/释放。

### LÖVE 做法

维护可复用的临时资源池，按格式+尺寸+MSAA 匹配：

```cpp
struct TemporaryTexture {
    Texture *texture;
    int framesSinceUse;  // 闲置帧数，用于回收
};
```

`getTemporaryTexture()` 先从池中找匹配的，找不到才新建。每帧清理超过 2 帧未使用的。

### 改进思路

1. 在 `OpenGLRenderDevice` 中添加 `TemporaryRenderTargetPool`
2. `createRenderTarget()` 先查池，找不到才 `glGenFramebuffers`
3. 释放时标记 `framesSinceUse = 0`，每帧递增
4. 超过 N 帧未使用的真正销毁

### 参考源码

- `src/modules/graphics/Graphics.cpp` 第 1324-1370 行
- `src/modules/graphics/Graphics.h` 第 1029-1055 行

---

## 13. 异步 Readback + Fence Sync

**优先级：1.00 (Impact 7 / Difficulty 7)**

### 当前问题

`readPixelsRGBA()` 完全同步（`glReadPixels` 阻塞 CPU 等待 GPU），高分辨率场景浪费一帧。

### LÖVE 做法

`GraphicsReadback` 提交后不阻塞，通过 GL fence / Vulkan fence 异步检查完成状态：

```cpp
struct ScreenshotInfo {
    ScreenshotCallback callback;
    void *data;
};
```

完成后触发回调，CPU 和 GPU 并行。

### 改进思路

1. 创建 `ReadbackRequest` 结构体，包含 callback + fence
2. 提交 `glReadPixels` 到 PBO（而非直接读回）
3. 每帧检查 fence 状态，完成后触发回调
4. 前置条件：#10（帧边界 present）

### 参考源码

- `src/modules/graphics/GraphicsReadback.h`
- `src/modules/graphics/opengl/GraphicsReadback.cpp`

---

## 14. 全局 Quad/Fan Index Buffer

**优先级：2.50 (Impact 5 / Difficulty 2)**

### 当前问题

矩形和扇形图元每次都重新生成索引数据。

### LÖVE 做法

初始化时预生成两张静态索引表，所有 quad/fan 绘制复用：

```cpp
// quad 索引: [0,1,2, 0,2,3, 4,5,6, 4,6,7, ...]
quadIndexBuffer->setImmutable(true);

// fan 索引: [0,1,2, 0,2,3, 0,3,4, ...]
fanIndexBuffer->setImmutable(true);
```

### 改进思路

1. 在 `OpenGLRenderDevice::initializeBackend()` 中生成 quad/fan 索引 VBO
2. 矩形/扇形绘制时直接绑定索引 VBO，不重新生成
3. 工作量极小，对高频图元有直接收益

### 参考源码

- `src/modules/graphics/Graphics.cpp` 第 265-310 行

---

## 15. Texel Buffer 抽象

**优先级：0.63 (Impact 5 / Difficulty 8)**

### 当前问题

字体渲染使用 CPU 端逐顶点展开（stb_easy_font），无法利用 GPU 并行。

### LÖVE 做法

支持 `BUFFERUSAGE_TEXEL`，Buffer 通过 `samplerBuffer` 在 shader 中按索引随机访问，用于 glyph 查询表。

### 改进思路

1. 这是 GPU 文本渲染的前置条件
2. 如果当前字体渲染满足需求，可延后
3. 先实现 #3（多后端抽象）再考虑

### 参考源码

- `src/modules/graphics/vertex.h` 第 74-87 行

---

## 16. DrawMode/ArcMode 组合枚举

**优先级：3.00 (Impact 6 / Difficulty 2)**

### 当前问题

`Paint::Style` 只有 `FILL / STROKE / FILL_AND_STROKE`，弧线的 open/close/pie 语义没有对应枚举。`Canvas.cpp` 中约 300 行重复的点生成代码。

### LÖVE 做法

用两层枚举覆盖所有 2D 图元变体：

```cpp
enum DrawMode { DRAW_LINE, DRAW_FILL };
enum ArcMode  { ARC_OPEN, ARC_CLOSED, ARC_PIE };
enum LineStyle { LINE_ROUGH, LINE_SMOOTH };
enum LineJoin  { LINE_JOIN_NONE, LINE_JOIN_MITER, LINE_JOIN_BEVEL };
```

`rectangle` 内部统一转成 `polygon(DRAW_FILL, 5个点)`，完全复用同一套三角化+索引逻辑。

### 改进思路

1. 在 `Paint` 或 `Canvas` 中添加 `DrawMode` / `ArcMode` 枚举
2. `drawRect` / `drawCircle` / `drawArc` 内部统一转成 polygon 点序列
3. 共享三角化+索引生成逻辑
4. 工作量小，API 一致性提升大

### 参考源码

- `src/modules/graphics/Graphics.h` 第 121-160 行
- `src/modules/graphics/Graphics.cpp` 第 2460-2649 行

---

## 17. Deprecation Warning 系统

**优先级：2.00 (Impact 4 / Difficulty 2)**

### 当前问题

API 演进时没有机制通知用户旧 API 已弃用。

### LÖVE 做法

`Deprecations` 类在 present 时统一绘制弃用警告图标：

```cpp
void Deprecations::draw(Graphics *gfx) {
    gfx->flushBatchedDraws();
    // 绘制小图标提示用户该 API 已弃用
}
```

### 改进思路

1. 在 `Console` 类中添加 `warnDeprecated(apiName)` 方法
2. 首次调用时记录，present 时统一输出警告
3. 避免重复输出

### 参考源码

- `src/modules/graphics/Deprecations.h` + `Deprecations.cpp`

---

## 18. isPixelFormatSupported 查询

**优先级：2.00 (Impact 6 / Difficulty 3)**

### 当前问题

`OpenGLRenderDevice` 不暴露格式查询。要支持 HDR canvas 或 compute shader 可写纹理时，无法提前知道硬件能力。

### LÖVE 做法

```cpp
// Graphics.h:859
virtual bool isPixelFormatSupported(PixelFormat format, uint32 usage) = 0;
```

`usage` 可以是 `SAMPLE / LINEAR / RENDERTARGET / BLEND / MSAA / COMPUTEWRITE` 的组合。上层代码据此自动降级。

### 改进思路

1. 在 `IRenderDevice` 中添加 `isFormatSupported(format, usage)` 接口
2. OpenGL 后端在初始化时查询 `GL_EXT_texture_format_sRGB_override` 等扩展
3. 用于临时 depth/stencil 的自动降级（参考 LÖVE 的降级链）

### 参考源码

- `src/modules/graphics/Graphics.cpp` 第 1149-1177 行

---

## 19. SpriteBatch 批量绘制

**优先级：1.33 (Impact 8 / Difficulty 6)**

### 当前问题

每个图元一个 Command + 一次 draw call，无法高效渲染大量同纹理精灵。

### LÖVE 做法

`SpriteBatch` 把大量同纹理精灵合并到一个 VBO，只发一次 draw call：

```cpp
spriteBatch:add(img, x, y)    // 追加到 VBO，不触发 GPU 上传
spriteBatch:add(img, x, y)    // 同上
spriteBatch:flush()            // 一次性上传 + draw call
```

### 改进思路

1. 这是游戏/UI 框架的高级功能
2. 前置条件：#1（统一 DrawCommand）+ #8（StreamBuffer）
3. 如果 WhatsCanvas 定位为纯 2D canvas API，可延后
4. 如果要做游戏框架，这是核心功能

### 参考源码

- `src/modules/graphics/SpriteBatch.h` + `SpriteBatch.cpp`

---

## 20. 窗口 resize 无痛重建

**优先级：1.50 (Impact 6 / Difficulty 4)**

### 当前问题

`Canvas::setSize()` 销毁并重建整个 FBO 和 texture，用户创建的 offscreen canvas resize 会丢失内容。

### LÖVE 做法

区分"系统 backbuffer"和"用户 canvas"。只有系统 backbuffer 在 resize 时重建：

```cpp
// opengl/Graphics.cpp:155
void Graphics::backbufferChanged(...) {
    // 只重建 internalBackbuffer + internalBackbufferDepthStencil
    // 用户创建的 Texture/Canvas 完全不受影响
}
```

`love.window.setMode` 也被改成了不清理 canvas 内容。

### 改进思路

1. `Canvas::resize()` 改为创建新 FBO + 复制旧内容，而非销毁重建
2. 或者更简单：区分 `resizeInternal()`（系统用）和 `resize()`（用户用，保留内容）
3. 对于 offscreen canvas，resize 时 copy 旧 texture 内容到新 texture

### 参考源码

- `src/modules/graphics/opengl/Graphics.cpp` 第 155-257 行

---

## 推荐实施顺序

### 第一阶段：基础架构（1-2 周）

| 顺序 | 改进项 | 理由 |
|------|--------|------|
| 1 | #16 DrawMode/ArcMode 枚举 | 2 天，为图元统一铺路 |
| 2 | #07 Shader validateDrawState | 1 天，调试体验立即提升 |
| 3 | #17 Deprecation Warning | 1 天，工程规范 |
| 4 | #14 全局 Index Buffer | 1 天，高频图元收益 |
| 5 | #18 isPixelFormatSupported | 2 天，能力查询基础 |

### 第二阶段：状态与管线（2-3 周）

| 顺序 | 改进项 | 理由 |
|------|--------|------|
| 6 | #05 GraphicsState 栈扩展 | 3 天，状态管理基础 |
| 7 | #10 帧边界 present/reset | 3 天，为批处理和异步铺路 |
| 8 | #06 Lazy Render Pass | 3 天，性能优化 |
| 9 | #11 Volatile 资源生命周期 | 5 天，健壮性基础 |
| 10 | #12 临时资源池 | 3 天，offscreen 性能 |

### 第三阶段：性能核心（3-4 周）

| 顺序 | 改进项 | 理由 |
|------|--------|------|
| 11 | #01 统一 DrawCommand | 5 天，批处理前置条件 |
| 12 | #08 StreamBuffer 顶点流 | 7 天，批处理前置条件 |
| 13 | #02 流式批处理系统 | 10 天，性能提升最显著 |

### 第四阶段：高级功能（按需）

| 顺序 | 改进项 | 理由 |
|------|--------|------|
| 14 | #20 窗口 resize 无痛重建 | 3 天，健壮性 |
| 15 | #19 SpriteBatch | 5 天，游戏框架需要 |
| 16 | #13 异步 Readback | 5 天，高分辨率场景 |
| 17 | #09 Gamma 校正 | 5 天，视觉正确性 |

### 第五阶段：长期目标（按需）

| 顺序 | 改进项 | 理由 |
|------|--------|------|
| 18 | #04 Canvas/Texture 统一 | 15 天，大重构 |
| 19 | #03 多后端抽象完善 | 20 天，跨平台 |
| 20 | #15 Texel Buffer | 7 天，GPU 文本渲染 |
