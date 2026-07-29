# 从慢 5.95 倍到快 31.8%：WhatsCanvas 对比 NanoVG 的性能优化实战

本文记录 WhatsCanvas OpenGL 路径渲染从首次 NanoVG 对比基线到第五轮优化，以及后续将同一套数据导向方法推进到 OpenGL 文本和 Vulkan 后端的完整过程。重点不是罗列“用了缓存”“减少了 draw call”这类结论，而是回答开发者真正关心的问题：

- 为什么两个功能相近的 2D 渲染器会相差接近 6 倍？
- 已经把几何合成一个 draw，为什么仍然不够快？
- 怎样用数据判断瓶颈位于几何、内存、上传、驱动还是 GPU？
- 每一轮优化具体删除了什么重复工作？
- 为什么最后一次优化没有减少顶点、上传量或 draw，却带来了最大的反超？
- 同一套 indexed geometry、紧凑属性、实例化和跨帧复用思路，如何迁移到 Vulkan 与 glyph atlas？

最终，在参考机器的 1920 × 1080 `geometry_stress` 合同场景中，WhatsCanvas 从 25.659 ms 降到 2.690 ms，整体加速约 9.54 倍；配对运行的 NanoVG GL3 为 3.947 ms，WhatsCanvas 快 31.8%。

后续提交继续优化了 OpenGL glyph atlas 和 Vulkan 的几何、文本、纹理提交路径。更新至 `4a6bf46` 后，同机八组 ABBA 复测中，OpenGL `geometry_stress` 为 2.851 ms，对应 NanoVG 3.999 ms，仍快 28.7%；`contract_text_latin` 为 3.091 ms，对应 NanoVG 3.364 ms，首次在这组配对样本中快 8.1%。Vulkan 的八进程中位数则达到几何 2.809 ms、图片 0.367 ms、文本 2.964 ms。

更新至 `bb966ff` 后，基准又从固定场景扩展为可改变规模、seed、数据和结构的参数化矩阵，并修复了大规模动态文本的布局缓存抖动。本轮进一步把同一参数语义接入 NanoVG adapter，并将公开比较升级为 4 个 ABBA block、每端 8 个独立进程和 bootstrap 95% 置信区间。最新质量门禁结果中，OpenGL 几何、图片、拉丁文本的进程中位数为 2.617、0.272、2.878 ms，NanoVG GL3 为 3.705、0.383、3.292 ms。Vulkan 内容签名 clip-mask 缓存还把 `clip_layers` 的五进程中位数从此前 31.34 ms 降到 8.80 ms。

更新至 `cac08c1` 后，OpenGL 图片批处理不再要求整批只有一张纹理，而是在不重排透明绘制的前提下使用 8 槽有序多纹理批次和独立 sampler；路径 position、color、coverage、index 也合并到一条帧上传流。相同 27 单元矩阵再次 27/27 通过质量门，结果从 WhatsCanvas / NanoVG / 无明确胜负的 12 / 12 / 3 变为 **12 / 11 / 4**。图片从 3/9 胜提升到 **6/9 胜**，1024 次 `dynamic-data` 从 4.273 ms 降至 0.773 ms，快于 NanoVG 的 1.667 ms；剩余差距已集中到大规模或结构变化几何，以及带真实 blend barrier 的 `dynamic-structure` 图片。

这些结果仍限定于特定机器、场景和同步方式，不表示 WhatsCanvas 的每种工作负载都快于 NanoVG，也不能把 Vulkan 数字与 NanoVG GL3 当作同后端排名。本文更重要的价值，是展示如何从一个可信的性能差距出发，逐层找到并删除真实浪费。

文中的代码均由对应提交的真实实现提炼而来，为突出思路省略了错误处理、日志、命名空间和部分兼容分支；完整实现应以文末列出的提交和当前源码为准。

## 1. 为什么选择 NanoVG 作为对照

NanoVG 是一个小而成熟的即时模式矢量渲染器。它的 API 和 WhatsCanvas 都能表达矩形、圆角矩形、圆、椭圆、路径、图片和基础文字，OpenGL GL3 后端也足够接近，因此很适合暴露轻量 2D 渲染器的结构性差异。

但“两个库都能画圆”并不等于可以直接比较。一个有效的跨库基准必须固定：

- 相同分辨率和操作数量；
- 等价的几何、颜色、混合、采样和抗锯齿要求；
- 相同字体和图片输入；
- Release 构建及设备、驱动信息；
- 相同 warmup 和采样数量；
- 同步到 GPU 完成，而不是拿异步提交时间对比完整帧时间；
- 像素质量门，防止通过少画、关闭 AA 或换成更简单效果“赢得”性能。

WhatsCanvas 的交叉库合同位于 [`benchmarks/cross_library/contract.json`](../benchmarks/cross_library/contract.json)，包含三个公共场景：

| 场景 | 工作负载 | 主要目的 |
| --- | ---: | --- |
| `geometry_stress` | 2304 个动态抗锯齿图形 | 检验路径构造、三角化、AA、合批和上传 |
| `image_grid` | 96 次缩放和部分圆角图片绘制 | 检验纹理复用、采样和 sprite batching |
| `contract_text_latin` | 576 次固定 Roboto 拉丁文本绘制 | 检验 shaping、glyph cache、atlas 和文本 batching |

标准环境为：

- Windows 10 x86_64；
- Intel Core i7-8700；
- NVIDIA GTX 1060 3GB，驱动 560.94；
- OpenGL 3.3；
- 1920 × 1080；
- 5 个 warmup 帧；
- 30 个计时帧；
- 每帧末尾调用 `glFinish`；
- readback 和像素哈希不计入帧时间。

初始基线使用 WhatsCanvas `0532fbd` 和 NanoVG `ce3bf745`。三次独立进程的中位数再取中位数，结果如下：

| 场景 | WhatsCanvas | NanoVG GL3 | 初始差异 |
| --- | ---: | ---: | --- |
| `geometry_stress` | 25.659 ms | 4.316 ms | NanoVG 快 5.95 倍 |
| `image_grid` | 0.308 ms | 0.372 ms | WhatsCanvas 快 1.21 倍，接近噪声区间 |
| `contract_text_latin` | 15.911 ms | 3.334 ms | NanoVG 快 4.77 倍 |

原始 JSONL、质量统计和依赖版本保存在 [`benchmarks/baselines/cross-library-nanovg-windows-i7-8700-gtx1060`](../benchmarks/baselines/cross-library-nanovg-windows-i7-8700-gtx1060/README.md)。

第一条重要线索已经出现：图片场景没有明显落后，几何和文字却很慢。这说明问题不是 OpenGL 初始化、窗口、计时器或整个渲染器都低效，而更可能集中在路径与文本的数据生产链路。

## 2. NanoVG 为什么快

NanoVG GL 后端采用非常直接的数据导向设计。它在一个 `GLNVGcontext` 中维护四组帧内连续数组：

```c
// 根据 NanoVG GL 后端简化
struct GLNVGcontext {
    GLNVGcall* calls;
    int ncalls;
    int ccalls;

    GLNVGpath* paths;
    int npaths;
    int cpaths;

    NVGvertex* verts;
    int nverts;
    int cverts;

    unsigned char* uniforms;
    int nuniforms;
    int cuniforms;
};
```

每次 `nvgFill()` 或 `nvgStroke()` 都向这些数组尾部追加：

1. 在 `calls` 中记录 draw 类型、纹理、路径范围、顶点范围和 uniform 偏移；
2. 在 `paths` 中记录每条子路径的 fill/stroke 顶点范围；
3. 把三角化后的几何复制到全帧 `verts`；
4. 把 paint、scissor、颜色和 stroke 参数写入 `uniforms`。

容量不足时数组按约 1.5 倍增长，容量足够后只移动尾部计数。帧结束只将 `ncalls/npaths/nverts/nuniforms` 清零，不释放容量。因此 warm frame 几乎没有逐图形的堆分配。

NanoVG 顶点也很紧凑：

```c
struct NVGvertex {
    float x;
    float y;
    float u;
    float v;
}; // 16 bytes
```

颜色不放在每个顶点里，而是放在每个逻辑 fill/stroke 的 fragment uniform 中。对于纯色 shape，无论它有 4 个还是 100 个顶点，颜色只保存一次。

到 `nvgEndFrame()`，NanoVG：

```text
一次上传全帧 uniform buffer
一次上传全帧 vertex buffer
顺序遍历 calls
按 call 发出 fill / fringe / stroke / triangles
```

需要注意，NanoVG 并没有把 2304 个 shape 合成一个 draw。凸填充通常仍会分别绘制主体 triangle fan 和 AA fringe triangle strip。它的优势不是 draw 数量少，而是：

- command/path/vertex/uniform 是连续数组；
- 每帧容量可复用；
- 顶点只有 16 字节；
- paint 按 shape 保存一次；
- 全帧集中上传；
- 没有在帧末重新把大量独立对象拼成另一套数据。

这给 WhatsCanvas 的第一条诊断方向是：不要只数 draw call，要检查产生 draw call 之前做了多少工作。

## 3. WhatsCanvas 最初慢在哪里

初始 WhatsCanvas 选择了功能优先的通用路径：

```text
Canvas API
  → 构造 Path / contour
  → 三角化
  → 生成 analytic-AA triangle soup
  → 每次绘制拥有自己的 vector
  → 创建独立 DrawPathCommand
  → Renderer::flush() 再把兼容命令合并
  → 重新变换、展开属性、重定位索引
  → 上传并绘制
```

可以把问题概括为：

> 过早展开，过晚合批（early expansion, late batching）。

矩形、圆和圆角矩形很早就失去了自己的语义，退化成通用 Path 和大量三角形。每个命令先拥有自己的点、颜色和 coverage；到 `flush()` 才发现这些命令可以合并，于是又完整遍历、复制和转换一遍。

原始热路径的典型形态近似如下：

```cpp
for (const DrawPathCommand& command : compatibleCommands) {
    for (std::size_t vertex = 0; vertex < command.vertexCount(); ++vertex) {
        merged.points.push_back(transformPoint(command, vertex));

        // 每个 shape 原本只需要一份颜色，
        // 为了跨命令单 draw，又展开成每顶点 RGBA。
        merged.colors.insert(
            merged.colors.end(),
            command.color,
            command.color + 4);

        merged.coverage.push_back(command.coverageAt(vertex));
    }

    for (std::uint32_t index : command.indices()) {
        merged.indices.push_back(baseVertex + index);
    }
}
```

即使最终只执行一次 `glDrawElements()`，CPU 仍可能为这一次 draw 重新构造数 MB 数据。

因此完整成本模型应当写成：

```text
帧时间 =
    API record
  + 几何/文字生成
  + frame compilation / batch assembly
  + CPU 内存分配与复制
  + buffer upload 与驱动提交
  + GPU 执行
  + GPU completion wait
```

“减少 draw call”只影响其中一部分。

初始统计暴露了几个具体问题：

- 2304 个简单图形生成超过 26 万个 AA triangle-soup 顶点；
- 一个干净的凸 `n` 边形在通用 AA 展开后大约产生 `9n - 6` 个顶点；
- 路径合批再次进行 transform、颜色、coverage 和索引处理；
- Path、contour、mesh、command 和 merged vectors 都产生短生命周期分配；
- translated primitive 因绝对坐标不同而难以复用局部几何；
- 文本热帧仍重复处理空格、glyph layout 和 quad 生成；
- OpenGL stream buffer 的早期使用方式容易重复覆盖同一段存储。

接下来的五轮优化没有重写整个渲染器，而是每次只删除一类已经被数据证明的重复工作。

## 4. Pass 1：先删除确定存在的重复工作

提交：`39f3d9a perf: eliminate repeated text and path staging work`

### 4.1 思路

第一轮不改变主要几何算法，目标是先清掉低风险、确定会发生的浪费：

- owning payload 应该移动，而不是复制；
- OpenGL 动态流应该每帧 orphan 一次，之后按 offset 追加；
- 空格等零面积 glyph 也必须进入缓存；
- 固定文本应该缓存解析后的 glyph layout；
- sprite quad 应共享四个角，而不是使用六个重复顶点；
- AA cache 要足以容纳场景稳定工作集，避免确定性的逐帧抖动。

### 4.2 实现

路径命令改为接收右值：

```cpp
DrawPathCommand::DrawPathCommand(DrawPathData data)
    : Command(Type::Path),
      data_(std::move(data))
{
}

renderer.submit(
    std::make_unique<DrawPathCommand>(
        std::move(fillData)));
```

StreamBuffer 从“每次上传覆盖 offset 0”改成帧内 append：

```cpp
void StreamBuffer::beginFrame()
{
    writeOffsetBytes_ = 0;
    orphanStorageOnce();
}

UploadRange StreamBuffer::uploadRange(
    const float* data, std::size_t count)
{
    const std::size_t offset =
        alignUp(writeOffsetBytes_, alignof(float));
    glBufferSubData(
        GL_ARRAY_BUFFER,
        static_cast<GLintptr>(offset),
        static_cast<GLsizeiptr>(count * sizeof(float)),
        data);
    writeOffsetBytes_ = offset + count * sizeof(float);
    return {buffer_, offset};
}
```

文字侧加入 resolved raster layout cache，并把零面积 glyph 视为有效缓存项。它们保留 advance 和 metrics，但不占用 atlas 像素：

```cpp
if (bitmap.width == 0 || bitmap.height == 0) {
    // 空格等 glyph 仍然缓存 metrics，
    // 下一个热帧不再进入 FreeType rasterizer。
    entries_[key] = makeAdvanceOnlyEntry(bitmap);
    return entries_[key];
}
```

SpriteBatch 从六个重复顶点改为四顶点、六索引：

```text
原来：0,1,2, 0,2,3 共 6 个完整顶点
现在：0,1,2,3 共 4 个顶点 + 0,1,2,0,2,3 索引
```

### 4.3 结果

| 场景 | 初始 | Pass 1 | 改善 | 对 NanoVG 差距 |
| --- | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 15.73 ms | 38.7% | 仍慢 3.64 倍 |
| `contract_text_latin` | 15.911 ms | 4.78 ms | 70.0% | 仍慢 1.43 倍 |
| `image_grid` | 0.308 ms | 0.29 ms | 噪声范围 | 无明确差距 |

几何 record/submit 从 12.212/13.067 ms 降到 8.66/6.95 ms。第一轮证明大量时间并不在 GPU，而在重复缓存缺失、对象复制和 stream staging。

但几何仍然把简单图形展开成重复 AA triangle soup，所以距离 NanoVG 还很远。

## 5. Pass 2：从重复三角形改为共享的 indexed AA

提交：`3357371 perf: index and share anti-aliased path geometry`

### 5.1 思路

analytic AA 生成的 triangle soup 中，大量三角形共享相同位置和 coverage 的顶点。继续上传重复顶点没有必要。

同时，缓存命中后如果仍把 points、coverage、indices 复制到每个命令，缓存只避免了计算，没有避免内存工作。更合理的方式是让命令共享不可变几何。

### 5.2 实现

AA 顶点使用 `(x bits, y bits, coverage bits)` 去重：

```cpp
struct AAIndexedVertexKey {
    std::uint32_t x;
    std::uint32_t y;
    std::uint32_t coverage;
};

for (std::size_t i = 0; i < soup.vertices.size(); ++i) {
    AAIndexedVertexKey key{
        floatBitPattern(soup.vertices[i].x),
        floatBitPattern(soup.vertices[i].y),
        floatBitPattern(soup.coverage[i])
    };

    if (auto found = vertexIndices.find(key);
        found != vertexIndices.end()) {
        indexed.indices.push_back(found->second);
        continue;
    }

    const std::uint32_t index =
        static_cast<std::uint32_t>(indexed.vertices.size());
    indexed.vertices.push_back(soup.vertices[i]);
    indexed.coverage.push_back(soup.coverage[i]);
    indexed.indices.push_back(index);
    vertexIndices.emplace(key, index);
}
```

缓存返回共享的不可变对象：

```cpp
struct DrawPathGeometry {
    std::vector<float> points;
    std::vector<float> coverage;
    std::vector<std::uint32_t> indices;
};

struct DrawPathData {
    std::shared_ptr<const DrawPathGeometry> sharedGeometry;
    // 复杂或动态情况仍可使用 owning vectors。
};
```

合批时先统计总量并一次性 `resize()`，再直接写入目标区间，避免反复 `push_back()` 和扩容。不同 affine transform 则直接写入最终坐标：

```cpp
merged.points.resize(totalVertices * 2);

for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
    const float x = source[vertex * 2];
    const float y = source[vertex * 2 + 1];
    destination[vertex * 2] =
        m00 * x + m10 * y + tx;
    destination[vertex * 2 + 1] =
        m01 * x + m11 * y + ty;
}
```

### 5.3 结果

| 指标 | Pass 1 之前的表示 | Pass 2 |
| --- | ---: | ---: |
| 顶点 | 269,598 个重复顶点 | 62,984 个唯一顶点 |
| 索引 | 无 | 269,598 |
| draw | 9 | 1 |
| path 上传 | 7,548,744 bytes | 2,841,944 bytes |
| `geometry_stress` | 15.73 ms | 8.68 ms |

相对原始基线快 66.2%，相对 Pass 1 又快 44.8%，与 NanoVG 的差距缩小到 2.01 倍。

这一轮的关键不是“用了索引”本身，而是同时做到：

- 几何去重；
- 缓存产物不可变共享；
- 合批目标预分配；
- 直接写最终 affine 结果；
- 把 9 个 packet 合成一个 indexed draw。

## 6. Pass 3：为简单图形保留语义，并压缩索引

提交：`9520f52 perf: compact indices and fast-path simple fills`

### 6.1 思路

Pass 2 已把上传量大幅降低，但 Rect、RRect、Circle、Oval 仍会构造完整 Path，并进入通用多 contour、fill rule、effect 检查流程。

对于满足以下条件的图形：

- 纯色 fill；
- 单 contour；
- winding fill；
- 无 shadow、gradient、path effect；
- 无复杂 stroke；

可以直接进入 simple fill path。复杂语义继续走原有通用路径，避免为了性能破坏功能正确性。

同时，当前 batch 只有 62,984 个唯一顶点，索引完全可以用 16 位表示。

### 6.2 实现

先做严格 eligibility 判断：

```cpp
bool canUseSimpleFillPath(const Paint& paint)
{
    return paint.getStyle() == Paint::Style::FILL
        && paint.getShaderType() == Paint::ShaderType::SOLID
        && !paint.hasShadowLayer()
        && !paint.hasCornerPathEffect();
}
```

常见 primitive 直接提交简单 contour 或缓存几何：

```cpp
if (canUseSimpleFillPath(paint)) {
    SimpleFillPrimitive primitive;
    primitive.kind = SimpleFillPrimitiveKind::Ellipse;
    primitive.width = radius * 2.0f;
    primitive.height = radius * 2.0f;
    primitive.ellipseSegments = segments;

    if (submitSimpleFillPrimitive(
            primitive, centerX - radius,
            centerY - radius, paint)) {
        return;
    }
}

// 不满足条件时保留完整 Path fallback。
drawPath(genericPath, paint);
```

batch 根据总顶点数选择索引格式：

```cpp
const bool useShortIndices =
    totalVertices
        <= static_cast<std::size_t>(
            std::numeric_limits<std::uint16_t>::max()) + 1u;

if (useShortIndices) {
    merged.shortIndices.push_back(
        static_cast<std::uint16_t>(
            baseVertex + sourceIndex));
} else {
    merged.indices.push_back(
        baseVertex + sourceIndex);
}
```

### 6.3 结果

| 指标 | Pass 2 | Pass 3 |
| --- | ---: | ---: |
| `geometry_stress` | 8.68 ms | 6.45 ms |
| record / submit | 未单独发布 | 3.07 / 3.40 ms |
| 索引字节 | 1,078,392 bytes（32 位） | 539,196 bytes（16 位） |
| path 总上传 | 2,841,944 bytes | 2,302,748 bytes |
| 与 NanoVG 差距 | 2.01 倍 | 1.49 倍 |

此时 WhatsCanvas 已经只有一次 indexed draw，但仍比 NanoVG 慢。这是一个重要转折点：继续优化 draw call 已经没有意义，必须检查属性格式和 CPU batch assembly。

## 7. Pass 4：参数化局部几何与紧凑属性

提交：`b287117 perf: compact primitive path submission`

### 7.1 思路

Pass 3 的 2,302,748 字节 path 上传可以精确拆分为：

| 数据 | 字节数 | 占比 |
| --- | ---: | ---: |
| position：62,984 × 2 × 4 | 503,872 | 21.9% |
| RGBA：62,984 × 4 × 4 | 1,007,744 | 43.8% |
| coverage：62,984 × 4 | 251,936 | 10.9% |
| uint16 index：269,598 × 2 | 539,196 | 23.4% |
| 合计 | 2,302,748 | 100% |

颜色是最大的单项流量。它虽然仍是 per-vertex 属性，但普通纯色并不需要 32 位浮点精度；coverage 同样只需有限精度。

另外，如果把圆和矩形以绝对坐标作为缓存键，相同尺寸但不同位置的图形仍无法共享。应将 primitive 规范化到局部空间，只把平移保留在 transform 中。

### 7.2 实现

普通合批纯色使用 RGBA8，coverage 使用 8 位 UNORM：

```cpp
std::uint8_t packUnorm8(float value)
{
    return static_cast<std::uint8_t>(
        std::clamp(
            std::lround(value * 255.0f),
            0l, 255l));
}

for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
    merged.packedColors.push_back(packUnorm8(color.r));
    merged.packedColors.push_back(packUnorm8(color.g));
    merged.packedColors.push_back(packUnorm8(color.b));
    merged.packedColors.push_back(packUnorm8(color.a));
    merged.packedCoverage.push_back(packUnorm8(coverage[vertex]));
}
```

OpenGL 使用 normalized attribute，在 shader 中仍得到 `[0,1]` 浮点值：

```cpp
glVertexAttribPointer(
    1, 4,
    GL_UNSIGNED_BYTE,
    GL_TRUE,
    4 * sizeof(std::uint8_t),
    colorOffset);

glVertexAttribPointer(
    2, 1,
    GL_UNSIGNED_BYTE,
    GL_TRUE,
    sizeof(std::uint8_t),
    coverageOffset);
```

任意 per-vertex float color 或启用 gamma correction 时保留原来的 float 路径。这种双路径让常见情况紧凑，同时不牺牲通用能力。

primitive cache key 改成尺寸、半径和分段数，而不是绝对位置：

```cpp
struct SimpleFillPrimitive {
    SimpleFillPrimitiveKind kind;
    float width;
    float height;
    std::array<float, 4> radii;
    std::array<int, 4> cornerSegments;
    int ellipseSegments;
};

// geometry 在 [0,width] × [0,height] 局部空间生成，
// left/top 只进入 transform。
```

同时加入 bounded thread-local `DrawPathCommandPool`，减少固定大小命令的堆分配；Release 对库内部生成的可信索引跳过重复全量边界扫描，Debug 继续保留验证。

### 7.3 结果

| 指标 | Pass 3 | Pass 4 |
| --- | ---: | ---: |
| `geometry_stress` | 6.45 ms | 4.682 ms |
| record / submit | 3.07 / 3.40 ms | 1.767 / 2.956 ms |
| path 上传 | 2,302,748 bytes | 1,357,988 bytes |
| 顶点 / 索引 | 62,984 / 269,598 | 不变 |
| draw / upload 次数 | 1 / 4 | 不变 |
| 配对 NanoVG | 4.316 ms 旧参考 | 3.965 ms |
| 差距 | 慢 1.49 倍 | 慢 1.18 倍 |

上传量下降 944,760 字节，恰好来自：

```text
RGBA float4 → RGBA8       节省 755,808 bytes
coverage float → UNORM8   节省 188,952 bytes
```

参数化曲线相对 Pass 3 只改变 2,073,600 个像素中的 171 个，即 0.0082%；每个变化通道只相差一个 8-bit level，RMSE 为 0.0069，质量门仍然通过。

Pass 4 已经接近 NanoVG，但还没有超过。此时顶点、属性和 draw 结构都已经足够紧凑，剩余问题转向“同一批数据为什么每帧还要重建”。

## 8. Pass 5：缓存最终 packet，而不只是缓存原材料

提交：`342ab53 perf: reuse stable OpenGL path batches`

### 8.1 思路

此前已经缓存：

- primitive local-space mesh；
- indexed AA geometry；
- shared points、coverage 和 indices。

但 `Renderer::flush()` 每帧仍会：

- 新建或重新扩容 merged vectors；
- 遍历 shared geometry；
- 给每个索引加 `baseVertex`；
- 再次生成 packed coverage；
- 复制完整 `Paint`；
- 为纯平移构造平移矩阵并执行通用 4 × 4 矩阵乘法。

如果一批 immutable shared geometry 的顺序没有变化，那么 rebased 16 位索引和 packed coverage 也是稳定的。缓存只停留在单个 shape 的中间结果还不够，应继续缓存更接近最终提交形态的数据。

### 8.2 实现

Renderer 保留跨帧 scratch packet：

```cpp
class Renderer {
    // 生命周期与 Renderer 相同，clear() 只清 size，不丢 capacity。
    DrawPathData pathBatchScratch_;

    // 上一帧 batch 中共享几何的身份和顺序。
    std::vector<
        std::shared_ptr<const DrawPathGeometry>>
        pathBatchTopology_;
};
```

合批时不再构造临时 `DrawPathData merged = first`，而是复用：

```cpp
DrawPathData& merged = pathBatchScratch_;
resetPathBatchState(first, merged);
```

然后检查 topology 是否稳定：

```cpp
bool reuseSharedTopology =
    immutableSharedTopology
    && useShortIndices
    && pathBatchTopology_.size() == batchSize
    && merged.shortIndices.size() == totalElements
    && merged.packedCoverage.size() == totalVertices;

if (reuseSharedTopology) {
    for (std::size_t i = 0; i < batchSize; ++i) {
        if (pathBatchTopology_[i]
                != commands[i].sharedGeometry) {
            reuseSharedTopology = false;
            break;
        }
    }
}

if (!reuseSharedTopology) {
    merged.shortIndices.clear();
    merged.packedCoverage.clear();
    rebuildTopologyAndCoverage();
}
```

为什么保存 `shared_ptr` 而不是只保存裸指针？

因为对象释放后 allocator 可能在同一地址创建另一个 geometry。仅比较地址可能产生假命中。保留共享所有权可以确保被比较的 topology 在缓存有效期内仍然是同一个对象。

回归测试同时覆盖：

- topology 不变时命中；
- shape 数量和大小相同但顺序改变时必须失效。

简单 fill 不再复制完整 `Paint`，而是直接从不可变 paint 与当前 graphics state 计算最终颜色和 blend mode。

平移矩阵也改成只更新实际受影响的一列：

```cpp
glm::mat4 translatedPathTransform(
    const glm::mat4& transform,
    float x, float y)
{
    glm::mat4 translated = transform;
    translated[3] =
        transform[0] * x
        + transform[1] * y
        + transform[3];
    return translated;
}
```

这避免了每个 primitive 创建一个通用平移矩阵，再执行完整矩阵乘法。

最后，Renderer 直接把复用的 packet 交给 `DrawPathProgram::draw()`，不再临时构造一个 merged `DrawPathCommand`。

### 8.3 结果

八个 WhatsCanvas 和八个 NanoVG 进程按 ABBA 顺序配对运行：

| 场景 | 原始 | Pass 4 | Pass 5 | 配对 NanoVG | 结果 |
| --- | ---: | ---: | ---: | ---: | --- |
| `geometry_stress` | 25.659 ms | 4.682 ms | **2.690 ms** | 3.947 ms | WhatsCanvas 快 31.8% |

分阶段时间：

| 实现 | record | submit |
| --- | ---: | ---: |
| WhatsCanvas Pass 5 | 1.486 ms | 1.199 ms |
| NanoVG GL3 | 1.611 ms | 2.372 ms |

`record`、`submit` 和 `total` 分别对各自的逐帧样本取中位数，因此两个分项的中位数之和不要求严格等于 `total` 中位数。

最有价值的证据是，Pass 4 到 Pass 5 之间这些指标完全不变：

```text
draw count      1
upload count    4
upload bytes    1,357,988
vertex count    62,984
index count     269,598
```

但总时间从 4.682 ms 降到 2.690 ms。说明收益不是来自减少 GPU 工作或降低画质，而是删除了 CPU allocation、topology reconstruction、coverage 重建、`Paint` 复制和通用矩阵计算。

所有八次 WhatsCanvas 运行都得到相同几何 hash `5e7e67fb8b9ca579`；图片和文字控制 hash 仍为 `432ad28b33a51375` 和 `737cad1b0d1169f2`。完整 Release 构建和 66 个测试均通过。

后续提交 `2cc2962 fix: decode packed path attributes safely` 又补充了：

- packed color/coverage 的统一安全解码；
- StreamBuffer 对齐后的 write offset 修复；
- command encoder 与 Vulkan 路径的回归测试。

## 9. 后续演进：把紧凑 packet 推进到 OpenGL 文本与 Vulkan

Pass 5 证明 OpenGL 路径可以通过“共享 indexed geometry + 紧凑动态属性 + 稳定 packet”超过 NanoVG，但当时这个结论还没有自然覆盖另外两条链路：

- 文本虽然已经合成很少的 draw，glyph quad 仍在 CPU 展开成四个大顶点；
- Vulkan 主路径仍会把 indexed path 重新展开为 triangle soup，再拼装新的 float vertex stream；
- Vulkan 每帧还会重复创建临时上传数组、descriptor set 和 command buffer 内容；
- 图片、文字和圆角纹理虽然都属于 textured quad，却因状态表达位置不同而难以共享实例批次。

因此，后续优化没有继续微调 NanoVG 对比场景中的某个循环，而是把前五轮学到的方法迁移到后端中立 `DrawList`、Vulkan 帧资源和 glyph atlas。

### 9.1 先让 Vulkan 保留 indexed geometry

提交 `74676e7 perf(vulkan): preserve indexed path geometry` 首先修复了一个典型的“上游已经压缩，下游又展开”问题。

优化前，command encoder 按 index 顺序复制位置、颜色和 coverage，使 Vulkan 重新得到三个顶点一组三角形的 triangle soup。OpenGL 前几轮刚删除的重复数据，在共享 `DrawList` 边界又出现了一次。

优化后，`DrawPrimitive` 显式携带唯一顶点和索引：

```cpp
struct DrawPrimitive {
    std::vector<float> positions;
    std::vector<std::uint32_t> indices;
    // ...
};

const bool retainIndices =
    path.hasIndices() && !path.hasShaderGradient();

if (retainIndices) {
    primitive.positions = decodeUniqueVertices(path);
    primitive.indices = copyTriangleIndices(path);
} else {
    expandTriangleSoupFallback(path, primitive);
}
```

Vulkan 为索引建立持久映射的 upload buffer，并在绘制时选择：

```cpp
if (draw.indexCount > 0) {
    vkCmdBindIndexBuffer(
        commandBuffer, indexBuffer,
        draw.indexOffset, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(
        commandBuffer, draw.indexCount, 1, 0, 0, 0);
} else {
    vkCmdDraw(
        commandBuffer, draw.vertexCount, 1, 0);
}
```

这一步的意义不是单纯把 `vkCmdDraw` 换成 `vkCmdDrawIndexed`，而是让 indexed AA 从 Canvas、command encoder、`DrawList` 一直存活到 Vulkan。数据结构一旦在中间层丢失语义，后端再聪明也无法恢复唯一顶点。

### 9.2 合并命令后只构造一次上传流

提交 `5eda476 perf(vulkan): stream merged geometry uploads` 继续处理 Vulkan 的 CPU staging。

旧路径对每个 primitive 建立局部 `std::vector<float>`，填充完成后再插入总上传数组。大量兼容纯色 path 也要先各自转成 `DrawPrimitive`，随后才尝试合并。优化后先扫描 frame，计算最终容量预算：

```cpp
std::size_t vertexFloatBudget = 0;
std::size_t indexBudget = 0;

for (const DrawPrimitive& primitive : drawList) {
    vertexFloatBudget +=
        vertexCount(primitive) * vertexStride(primitive);
    indexBudget += primitive.indices.size();
}

vertexUpload.reserve(vertexFloatBudget);
indexUpload.reserve(indexBudget);
```

然后让每个 draw 直接追加到一组 frame upload stream：

```cpp
draw.vertexOffset =
    vertexUpload.size() * sizeof(float);

for (std::size_t vertex = 0;
     vertex < vertexCount; ++vertex) {
    vertexUpload.push_back(x);
    vertexUpload.push_back(y);
    appendColorAndCoverage(vertexUpload, primitive, vertex);
}
```

对于连续、blend/scissor 兼容的纯色路径，转换和合并在同一条路径完成，索引只做一次 rebasing。对应回归测试同时覆盖：

- 多个 indexed quad 合成一个 batch；
- uniform color 与 packed vertex color 混用；
- coverage 存在或缺省；
- 合并后索引仍指向正确的唯一顶点。

### 9.3 用多帧资源删除 Vulkan 的同步提交串行化

提交 `20cf99c perf(vulkan): pipeline asynchronous frame uploads` 把单套“上传、录制、提交、等待”资源改为三个可轮换的 frame slot。每个 slot 保留：

```text
fence
command buffer
mapped vertex/index upload
gradient uniform buffer
descriptor pool
```

新帧优先取得已经 signaled 的 slot；GPU 忙时可以使用另一个 slot，而不是立即等待刚提交的 fence。纹理 quad 也先按 texture、sampling、blend、scissor 等兼容状态合并，动态 alpha 进入 packed tint。

这一轮解决的是提交结构，而不是某个 shader 算得慢：

```text
旧：
    CPU build
      → upload
      → record
      → submit
      → wait
      → next frame

新：
    frame N CPU build/upload/submit
    frame N+1 CPU build/upload/submit
             GPU may still consume frame N
```

基准仍在每帧末端同步到 GPU 完成，以保持与 OpenGL/NanoVG 合同一致；多 frame slot 的价值主要是避免后端内部过早串行化，并让正常异步宿主可以获得真实收益。

### 9.4 glyph atlas 从 RGBA8 变成 Alpha8

文字 atlas 保存的是 coverage，不需要为每个 texel 重复存储白色 RGB。提交 `bc97a82 perf(vulkan): compact glyph atlas rendering` 为渲染设备增加 Alpha8 资源接口，并让 Vulkan 使用 `VK_FORMAT_R8_UNORM`；随后 `1baed30` 补齐 OpenGL 单通道纹理。两边通过采样或 view swizzle 恢复：

```text
sample.rgb = 1
sample.a   = coverage
```

因此 2048 × 2048 atlas 从：

```text
RGBA8：2048 × 2048 × 4 = 16 MiB
Alpha8：2048 × 2048 × 1 = 4 MiB
```

下降到原来的四分之一。当前 `contract_text_latin` 的公开统计也从历史基线中的 16,777,216 字节降为 4,194,304 字节。

更重要的是，Alpha8 让 glyph atlas 成为可以明确识别的资源类型，后端能够为它选择更简单的 shader、实例格式和 dirty-rect 更新路径，而不必把所有图片都塞进最通用的 RGBA 分支。

### 9.5 一个 glyph 只上传一个 instance

提交 `c6563f3 perf(vulkan): streamline glyph atlas batches` 和 `1baed30 perf(opengl): instance glyph atlas rendering` 分别在 Vulkan 和 OpenGL 上实现 compact glyph instance。

OpenGL 旧 `SpriteBatch` 为每个 glyph 生成四个顶点，每个顶点 13 个 float：

```text
4 vertices × 13 floats × 4 bytes = 208 bytes / glyph
```

新路径对轴对齐的 Alpha8 atlas quad 只记录：

```cpp
struct GlyphInstance {
    float x0, y0, x1, y1;  // bounds
    float u0, v0, u1, v1;  // atlas rect
    float r, g, b, a;      // tint
};                         // 48 bytes
```

四个静态角点由 `gl_VertexID` 生成：

```glsl
int vertex = gl_VertexID & 3;
vec2 corner = quadCorner(vertex);
vec2 position = mix(aBounds.xy, aBounds.zw, corner);
vUv = mix(aUvRect.xy, aUvRect.zw, corner);
```

最终使用：

```cpp
glDrawArraysInstanced(
    GL_TRIANGLE_STRIP, 0, 4, glyphCount);
```

CPU 每 glyph 的动态数据从 208 字节降到 48 字节，约减少 76.9%，同时不再逐 glyph 计算四个顶点。旋转、斜切或非 Alpha8 图片仍走原来的通用 vertex fallback，因此紧凑路径没有扩大功能假设。

Vulkan 使用相同思想：Alpha8 glyph batch 变成 `TexturedQuadInstance`，packed tint 和 UV/bounds 随实例上传，静态 quad 由 instanced vertex shader 展开。`DrawImageBatchCommand` 也让 Canvas 层已经形成的 glyph batch 可以直接穿过 Renderer，避免先拆成许多 `DrawImageCommand` 再重新识别。

### 9.6 Vulkan 纯色几何也收敛到 16 字节顶点

提交 `e72b9d9 perf(vulkan): accelerate merged geometry` 把 Pass 4 在 OpenGL 验证过的紧凑属性格式带到 Vulkan：

```cpp
struct CompactSolidVertex {
    float x;
    float y;
    std::uint32_t color;   // RGBA8_UNORM
    std::uint8_t coverage; // UNORM8
    std::uint8_t padding[3];
};
static_assert(sizeof(CompactSolidVertex) == 16);
```

同时增加三项配套优化：

1. 顶点不超过 65,536 时保留 `uint16_t` index；
2. producer 已验证索引时设置 `indicesTrusted`，Release backend 不再完整扫描一次；
3. 单个 merged solid draw 可以直接把 `compactVertices` 和 short indices 写入当前 frame 的 mapped buffer，跳过“转成 float scratch，再 memcpy 到 mapped memory”的中间形态。

核心 fast path 可以概括为：

```cpp
if (singleMergedSolidDraw
    && primitive.compactSolidAttributes
    && !primitive.compactVertices.empty()) {
    memcpy(
        frame.vertexMapping,
        primitive.compactVertices.data(),
        primitive.compactVertices.size()
            * sizeof(CompactSolidVertex));
    copyShortIndicesDirectly(frame, primitive);
}
```

Vulkan 后端还把 vertex/index scratch 保存在 device 对象中跨帧复用，避免每次 `executeDrawList` 创建和释放大数组。至此，OpenGL 和 Vulkan 虽然使用不同 API，但热点数据都收敛到“float2 position + RGBA8 + coverage8 + short index”。

### 9.7 稳定纹理帧复用 descriptor 与 command buffer

提交 `4a6bf46 perf(vulkan): streamline texture frame submission` 处理已经紧凑之后仍然存在的固定成本：

- stable texture/sampler/clip 组合在 frame slot 内复用 descriptor set；
- gradient descriptor 按 slot 复用，只更新对应 uniform range；
- 对 pipeline、render target、descriptor、draw offset/count 和 push constants 计算 command signature；
- signature 未变时复用已录制的 command buffer，只更新 mapped vertex/index payload；
- rounded radius、width、height 进入每实例数据，使圆角与非圆角 quad 可以留在同一个 textured batch。

最后一点把测试中的四个交错圆角/直角图片从两个 draw 合成一个 draw。command buffer 复用测试还会改变下一帧 tint，确保“命令稳定”不会错误地把动态顶点内容也冻结：

```text
稳定：
    pipeline
    descriptor binding
    vertex/index buffer handle
    draw count and offsets

动态：
    mapped instance bytes
    tint / bounds / UV / rounded parameters
```

这与 Pass 5 的思想一致：缓存的是不会变化的提交结构，而不是盲目缓存整帧像素或动态属性。

### 9.8 `4a6bf46` 阶段结果

文档更新时在同一台 Windows 10、i7-8700、GTX 1060 3GB、驱动 560.94 机器上，以 Release、1920 × 1080、5 warmup、30 timed frames、GPU 完成同步重新验证当前 `4a6bf46`。

三个实现的当前结果如下。数值都是同步到 GPU 完成的完整帧中位数，**越低越好**：

| 场景 | 负载 | WhatsCanvas OpenGL | WhatsCanvas Vulkan | NanoVG GL3 | 直观结论 |
| --- | ---: | ---: | ---: | ---: | --- |
| `geometry_stress` | 2304 个动态 AA 图形 | 2.851 ms | **2.809 ms** | 3.999 ms | Vulkan 与 OpenGL 接近，均明显快于 NanoVG |
| `image_grid` | 96 次图片绘制 | **0.287 ms** | 0.367 ms | 0.307 ms | OpenGL 最快，但最大差值仅 0.080 ms，噪声敏感 |
| `contract_text_latin` | 576 次固定文本绘制 | 3.091 ms | **2.964 ms** | 3.364 ms | Vulkan 最快，OpenGL 也略快于 NanoVG |

OpenGL 与 NanoVG 各运行八个独立进程，使用 ABBA 顺序，对“每个进程的帧中位数”再次取中位数。对应三场景中，WhatsCanvas OpenGL 分别比 NanoVG 快 28.7%、6.6% 和 8.1%。Vulkan 列是八个独立 WhatsCanvas 进程的中位数；NanoVG adapter 只有 OpenGL，因此 Vulkan 数字用于观察同机后端表现，不是严格的同 API NanoVG 排名。

几何的最新数字与 Pass 5 的 2.690 / 3.947 ms 不完全相同，这是独立进程、系统噪声和驱动调度造成的正常波动；两组配对样本都支持“该合同场景已经反超”，不应把 31.8% 与 28.7% 的差值解释为后续提交造成性能回退。

文字是这一轮的新进展。初始基线为 15.911 ms、NanoVG 为 3.334 ms；经过 layout/glyph cache、atlas batching、Alpha8 和实例化后，当时的配对样本达到 3.091 ms，对原始 WhatsCanvas 快约 80.6%，并首次略快于 NanoVG。那组 8.1% 的领先明显小于几何，因此推动了后续自动 ABBA 与置信区间工作。

图片场景中，OpenGL 0.287 ms、NanoVG 0.307 ms，绝对差只有 0.020 ms。这个结果说明 WhatsCanvas 没有落后，但不适合宣传成稳定的数量级优势。

OpenGL 几何和文字 hash 仍为 `5e7e67fb8b9ca579`、`737cad1b0d1169f2`，与优化前的质量控制值一致。新增 indexed geometry、compact solid、Alpha8 更新、实例 batch 和动态 tint command-buffer 复用都有回归覆盖；本次更新针对性运行的四个 Release 测试全部通过。

### 9.9 参数化矩阵：避免只对固定对象数优化

固定的 `geometry_stress`、`image_grid` 和 `contract_text_latin` 适合作为跨版本、跨库回归锚点，但任何单一对象数都可能被质疑为针对性优化。提交 `3f82973 feat(benchmarks): add parameterized performance matrix` 为几何、图片和文字增加了以下维度：

- `stable`：资源、内容和命令 topology 保持稳定；
- `dynamic-data`：位置、颜色或文本选择变化，但命令结构稳定；
- `dynamic-structure`：图元、纹理、文本、字号、样式和部分渲染状态都可以变化；
- `--operations`、`--seed`、`--texture-count`、`--rounded-ratio`、`--state-change-rate`、`--text-length`：控制规模、资源基数和变化强度。

runner 会让每个 workload/seed 在独立 Release 进程中运行，保存原始 JSONL，并输出 JSON、CSV、Markdown 和缩放曲线。这样可以同时看到缓存命中路径、数据变化路径、结构变化路径，以及跨越单批上限后的增长曲线。

参数化矩阵立即暴露了一个真实问题：1024 次生成文本绘制、32 字符、6.25% 状态变化的 `dynamic-structure` 工作负载中，OpenGL 一度需要 61.49 ms。它不是 GPU 填充率问题，record 已占 59.33 ms；旧文本缓存只有 512 项，而工作集超过缓存容量，并且每次 LRU touch 都在线性扫描 `deque`，导致大量 shaping/layout 条目反复淘汰和重建。

提交 `58b5129 perf(text): eliminate dynamic layout cache thrashing` 将字符串 key 纳入通用哈希 LRU、把 shape/layout 容量提高到 2048，并让 layout 缓存保存与绘制坐标无关的 glyph quad。三组 seed、标准 profile（5 warmup + 30 timed）、1920 × 1080 的进程中位数再次取中位数如下：

| OpenGL 1024 次文本绘制 | 优化前 | 当前 | 改善 |
| --- | ---: | ---: | ---: |
| `stable` | 7.99 ms | 6.17 ms | 快 22.8% |
| `dynamic-data` | 9.28 ms | 6.26 ms | 快 32.5% |
| `dynamic-structure` | 61.49 ms | **7.65 ms** | **快 87.6%，约 8.04×** |

同一组当前 workload 在 Vulkan 上分别为 5.84、5.76、8.07 ms。`dynamic-structure` 仍会产生约 116 至 127 个 draw，而稳定和数据变化场景只需要 2 个 draw；即使如此，两后端都保持在约 8 ms，而不是依赖固定字符串和单一提交结构才获得好成绩。

NanoVG adapter 现在也实现了相同的 workload mode、随机函数、规模、seed、纹理数、圆角比例、状态变化率和文本长度语义，因此参数化场景可以进入同一质量门与 ABBA runner。固定合同仍用于跨版本锚点，参数化比较用于验证结论能否跨规模和变化模式成立。

### 9.10 Vulkan 图层合成：删除一次全尺寸复制

参数化矩阵完成后，14 个固定 1080p 场景的扫描将 `clip_layers` 定位为 Vulkan 最明显的通用瓶颈：144 次嵌套裁剪、变换和图层在 Vulkan 上需要 41.38 ms，而 OpenGL 为 16.92 ms。profile 显示 Vulkan 的 40.89 ms 几乎全部发生在 record 阶段。

旧 Vulkan 离屏链路为了返回可采样图像，会执行：

```text
pooled render target
  -> render layer commands
  -> allocate independent sampled texture
  -> copy the full layer image
  -> composite sampled texture
```

这个独立纹理对异步滤镜是必要的，但普通 `saveLayer` 会在恢复时立即合成，不需要再复制一份同尺寸像素。提交 `bb966ff perf(vulkan): avoid copies for direct layer composition` 为离屏请求增加受控的 `allowDirectTargetSampling`：无 image filter、无 backdrop filter 时，把 render target 转换为 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` 并直接返回其 image resource；资源仍被合成命令持有，因此 render-target pool 不会过早复用它。

带滤镜的图层明确不启用这条路径。它们仍使用独立 sampled texture，以保证异步滤镜输入的生命周期和确定性；优化过程中观察到的滤镜 hash 波动也因此没有被带入最终实现。

| Vulkan `clip_layers`，1080p | 完整帧 | record | submit | FPS | 像素 hash |
| --- | ---: | ---: | ---: | ---: | --- |
| 优化前参考进程 | 41.377 ms | 40.894 ms | 0.691 ms | 24.2 | `4cce05d0b8531e45` |
| 优化后进程 1 | 29.588 ms | 29.281 ms | 0.310 ms | 33.8 | `4cce05d0b8531e45` |
| 优化后进程 2 | 32.877 ms | 32.562 ms | 0.323 ms | 30.4 | `4cce05d0b8531e45` |
| 优化后进程 3 | 31.344 ms | 31.045 ms | 0.318 ms | 31.9 | `4cce05d0b8531e45` |

优化后三进程中位数为 31.344 ms，相对优化前参考进程快 24.2%。优化前只有一个同配置参考进程，因此这个百分比适合作为明确的方向性改善，不应冒充置信区间；三次优化后结果和一致像素 hash 则说明收益与输出都可重复。

这一阶段 Vulkan 仍约为 OpenGL 16.92 ms 的 1.85 倍，说明最终复制并不是唯一成本。后续 profile 继续定位到另一个更具体的问题：clip path 每帧会创建新的资源对象，但圆角边界、coverage 和 transform 实际保持不变；按对象地址缓存因此每帧失效，并反复生成 16 张全画布 mask。

后续优化为 clip geometry、coverage、transform 和画布尺寸生成双 64-bit 内容签名，使用带 128 MiB 上限的 LRU 跨帧复用 mask；不同 transform 有专门像素回归，防止错误复用。复杂 gradient clip 则直接渲染到可采样 GPU target，再由已有双纹理 shader 同时采样内容和 mask，删除同步 readback、CPU unpremultiply/coverage 合成和纹理重传。五个独立 Standard 进程得到：

| 后端 | `clip_layers` 进程中位数 | 五进程范围 | 像素 hash |
| --- | ---: | ---: | --- |
| Vulkan | **8.801 ms** | 7.673–12.158 ms | `4cce05d0b8531e45`（5/5 一致） |
| OpenGL | 18.078 ms | 17.552–18.354 ms | `c5c9c864ea32cd73`（5/5 一致） |

相对前一阶段 Vulkan 31.344 ms，中位数改善约 71.9%。这不是把裁剪移出场景：144 个命令、16 次 render-target switch 和最终像素输出均保留；收益来自稳定裁剪内容不再因临时对象身份变化而重复栅格化。

### 9.11 当前跨库复测

参数化矩阵和文本缓存修复完成后，使用自动化 runner 运行 4 个 ABBA block。每端每场景启动 8 个独立进程，使用 Release、1920 × 1080、5 warmup、30 timed frames 和 GPU 完成同步；表中括号为进程样本 bootstrap 95% 置信区间：

| 场景 | WhatsCanvas OpenGL | NanoVG GL3 | ABBA 配对 NanoVG / WhatsCanvas | 质量门 |
| --- | ---: | ---: | ---: | --- |
| `geometry_stress` | **2.617 ms**（2.572-2.764） | 3.705 ms（3.605-3.807） | 1.407×（1.273-1.441） | PASS |
| `image_grid` | **0.272 ms**（0.262-0.304） | 0.383 ms（0.380-0.388） | 1.369×（1.347-1.417） | PASS |
| `contract_text_latin` | **2.878 ms**（2.670-3.021） | 3.292 ms（3.255-3.337） | 1.153×（1.105-1.169） | PASS |

三组配对比的 95% 区间都没有跨过 1.0，但这仍只代表当前机器和合同，不应宣传成跨硬件全局排名。48 个 adapter 进程全部通过各自场景的像素 MAE、RMSE 和 changed-pixel 阈值；文字因为字体栅格器不同允许受控差异，不代表两张图逐像素相同。

### 9.12 有序多纹理批处理与统一几何上传流

参数矩阵暴露了一个固定 `image_grid` 看不到的问题：只要四张纹理随机交错，旧 Renderer 就会因为“必须与上一张纹理相同”而频繁断批。1024 次 `dynamic-data` 虽然 record 只需约 0.3 ms，submit 却达到 3.953 ms，产生 754 次 draw；32 纹理并加入圆角和 blend 变化时达到 999 次 draw。

新路径给每个顶点增加一个纹理槽，在保持命令顺序的情况下让一个批次引用最多 8 张纹理：

```text
ordered image commands
  → reuse existing texture slot
  → allocate one of eight slots
  → only split at a real state barrier or ninth distinct texture
  → one indexed sprite draw
```

采样状态使用批处理专用 sampler object，批次结束后解绑；Renderer 随后显式失效 unit 0 的纹理状态缓存。它既避免每批反复设置四个 texture parameter，也不会把 Linear + Clamp 状态泄漏给后续 Nearest、Repeat 或 clip 绘制。

几何侧没有把 99,770 顶点的两个 16 位索引批次强行合并。那样会把约 849 KB 索引翻倍为 32 位，节省一次 draw 却增加接近 0.85 MB 上传。实际优化是让 position、packed color、coverage 和 index 共用一条已预留完整 packet 容量的 StreamBuffer，把每帧四次 storage orphan 收敛为一次，同时保留 16 位索引。

最新 Standard 矩阵每个单元执行 2 个 ABBA block、每端 4 个独立进程，分类结果如下：

| 类别 | WhatsCanvas 领先 | NanoVG 领先 | 无明确胜负 |
| --- | ---: | ---: | ---: |
| 几何 | 2 | 6 | 1 |
| 图片 | 6 | 3 | 0 |
| 文本 | 4 | 2 | 3 |
| **合计** | **12** | **11** | **4** |

图片的六个 stable / dynamic-data 单元全部领先。三个 `dynamic-structure` 单元仍落后 21.0%-37.6%，因为它们刻意插入 blend mode 变化；这些是不能跨越的透明顺序 barrier，不应通过重排绘制来“优化”。几何仍是最大差距：1024 dynamic-data、全部 dynamic-structure，以及 4096 的三种模式均由 NanoVG 领先。文本仅有 1024 stable / dynamic-data 小幅落后 0.7% / 4.5%。

## 10. 性能差距是怎样一步步缩小的

### 10.1 完整帧时间

| 阶段 | `geometry_stress` | 相对原始 | 相对 NanoVG |
| --- | ---: | ---: | --- |
| 原始 | 25.659 ms | 1.00× | 慢 5.95 倍 |
| Pass 1 | 15.73 ms | 快 38.7% | 慢 3.64 倍 |
| Pass 2 | 8.68 ms | 快 66.2% | 慢 2.01 倍 |
| Pass 3 | 6.45 ms | 快 74.9% | 慢 1.49 倍 |
| Pass 4 | 4.682 ms | 快 81.8% | 慢 1.18 倍，配对 NanoVG 3.965 ms |
| Pass 5 | **2.690 ms** | **快 89.5%，约 9.54×** | **比配对 NanoVG 3.947 ms 快 31.8%** |

NanoVG 数值在 Pass 4/5 使用新的交替或 ABBA 配对运行，因此不能把不同阶段的 NanoVG 小幅波动解释为库本身发生了变化。

### 10.2 数据结构演进

```text
原始：
    269,598 个重复 AA 顶点
    float position + float4 color + float coverage
    9 draws
    7,548,744 path bytes

Pass 2：
    62,984 个共享顶点 + 269,598 indices
    immutable shared geometry
    1 draw
    2,841,944 path bytes

Pass 3：
    uint16 index
    simple primitive fast path
    1 draw
    2,302,748 path bytes

Pass 4：
    parameterized local-space primitive mesh
    RGBA8 + coverage8
    command pool
    1 draw
    1,357,988 path bytes

Pass 5：
    持久 merged packet
    稳定 topology / coverage 跨帧复用
    1 draw
    1,357,988 path bytes
```

这条曲线说明优化不是单一技巧，而是四种收益叠加：

1. 删除重复计算；
2. 删除重复数据；
3. 使用更紧凑的表示；
4. 利用跨帧时间一致性，不再重建最终 packet。

## 11. 为什么最终能超过 NanoVG

NanoVG 的优势一直存在：

- 连续 frame arrays；
- 16 字节顶点；
- per-call paint uniform；
- 集中的 vertex/uniform 上传；
- warm frame 很少分配。

但 NanoVG 仍按 call 顺序发出大量小 draw；凸填充通常包含主体和 AA fringe 两次绘制。

WhatsCanvas 最终形成了不同的组合：

- indexed AA 将重复顶点变成共享顶点；
- compatible paths 合成一次 draw；
- RGBA8/coverage8 将属性流压紧；
- primitive 保留语义并共享局部空间 mesh；
- merged packet 跨帧保留容量；
- 稳定 index topology 和 coverage 直接复用；
- 动态部分只重建 position，以及由 per-shape color 展开的 packed color stream；
- 简单 fill 避免完整 Path、`Paint` 和通用矩阵成本。

也就是说，WhatsCanvas 没有机械复制 NanoVG，而是同时吸收了它的数据导向优势，并保留了单 draw、indexed geometry 和更完整 Canvas 架构的优势。

这也是本次对比最重要的意义：竞品代码应当作为“架构探针”，帮助定位自己为什么慢，而不是作为必须逐行照搬的答案。

## 12. 可以复用到其他渲染器的原则

### 12.1 Draw call 是指标，不是目标

一次 draw 之前可能有数毫秒的 frame compilation。必须同时统计：

- 输入命令数；
- 三角化前后顶点数；
- 索引数；
- batch 数和 draw 数；
- upload 次数与字节；
- cache hit/miss；
- command allocation；
- record、endFrame CPU、driver submit 和 GPU wait。

### 12.2 保留语义，延迟展开

Rect、RRect、Circle、GlyphRun 和 Image 应尽可能保持 typed primitive。过早退化为通用 Path 会丢失：

- topology 稳定性；
- 参数化缓存机会；
- 局部空间共享；
- 更紧凑的实例或 material 表达。

### 12.3 缓存最终形态

缓存 tessellation 只能避免计算；如果每帧还要把缓存结果复制、转换、重定位并量化，仍然会很慢。

缓存层次可以逐步推进：

```text
源数据解析结果
  → 局部 geometry
  → AA-expanded indexed geometry
  → batch topology
  → 最终 draw packet
```

越接近提交端，命中一次能够删除的工作越多。

### 12.4 利用时间一致性

UI 和 2D 场景通常只有 transform、颜色、透明度或少量内容变化。geometry、indices、coverage 和 pipeline 类型常常连续数百帧不变。

可以将 packet 划分为：

```text
稳定部分：
    geometry
    indices
    coverage
    material/pipeline 类型

动态部分：
    transform / position
    color
    opacity
    clip / scissor
```

稳定部分跨帧复用，动态部分定点更新。

### 12.5 为常见路径设计紧凑格式

不要因为少数复杂情况，就让所有顶点承担最昂贵格式：

- 普通纯色：RGBA8；
- 普通 coverage：UNORM8；
- 小 batch：uint16 index；
- 任意渐变或高精度数据：保留 float fallback。

双路径通常比“所有情况统一成最大格式”更合适。

### 12.6 缓存必须有明确身份和失效规则

不能只比较指针地址或 vector 长度。至少要测试：

- 正常命中；
- 内容改变；
- 顺序改变；
- 大小相同但 topology 改变；
- 资源释放后重新分配；
- cache entry 生命周期。

性能缓存如果没有 invalidation 测试，通常只是延迟出现的渲染错误。

### 12.7 热路径避免不必要的通用操作

在每帧只执行一次时，复制 `Paint` 或做一次 4 × 4 矩阵乘法不值得关注；在 2304 个 primitive 上重复时就会成为热点。

应针对热路径问：

- 真正变化的是整个对象，还是其中四个字段？
- 真正需要通用矩阵乘法，还是只需要更新平移列？
- 真正需要创建命令对象，还是可以直接写 packet？

## 13. 基准结论的边界

本文的“反超 NanoVG”严格限定为：

> Windows i7-8700 / GTX 1060、OpenGL 3.3、1920 × 1080、`geometry_stress`、同步完整帧、通过质量门的配对 ABBA 结果。

还不能扩大为所有工作负载的全局结论。最新参数矩阵进一步给出了明确边界：

- 4096 图形的 stable / dynamic-data / dynamic-structure 分别为 6.619 / 6.636 / 9.425 ms，NanoVG 为 4.542 / 4.711 / 5.647 ms；固定 2304 图形合同的领先不能线性外推；
- 图片 stable 和 dynamic-data 已在三个规模全部领先，但包含 32 纹理、50% 圆角和 12.5% 状态切换的 dynamic-structure 仍落后；
- `contract_text_latin` 在 1024 stable / dynamic-data 上仅落后 0.7% / 4.5%，差距很小；dynamic-structure 仍以 6.652 ms 领先 NanoVG 的 8.794 ms；
- Vulkan 已有同机八进程结果，但 NanoVG adapter 只有 OpenGL，不能把 Vulkan 2.809 / 0.367 / 2.964 ms 写成对 NanoVG 的同后端胜负；
- Software 和 OpenGL ES 仍需要各自验证；
- 复杂 path、stroke、gradient、clip、layer 和 filter 不一定命中 simple-fill/stable-topology 路径；
- 当前 `geometry_stress` 每帧只改变水平平移，图形尺寸、类型、顺序和 topology 都稳定，不代表每帧任意变形；
- 当前 62,984 个路径顶点刚好低于 65,536 单批上限；保持相同顶点密度扩展到 5000 个图形会拆成约三个批次，不能线性外推 2304 个图形的领先幅度；
- 当前 runner 已发布 bootstrap 95% 置信区间，但仍需要更多硬件和驱动样本；
- 两端现在都把不透明全屏 src-over 绘制作为测量内的清屏语义；NanoVG 仅在测量区间外做 stencil 维护；
- 文字场景允许 FreeType/HarfBuzz 与 stb/fontstash 的正常 shaping、kerning 和 rasterization 差异。

因此文章描述的是一个真实、可重复、质量受控的场景优化过程，而不是营销式的库排名。

## 14. 下一步

几何 Pass 5 和后续跨后端提交证明 stable compiled packet 很有价值。Alpha8、glyph instancing、Vulkan compact solid upload 和 command-buffer reuse 已经完成第一步，接下来仍有以下工作。

### 14.1 文本

实例化已经把每 glyph 的提交数据从 208 字节降到 48 字节，并显著降低 submit 时间；当前文字场景剩余成本更多位于 record。稳定文本还可以缓存更接近最终提交形态的 `GlyphRun`：

```text
UTF-8
  → shaping
  → glyph resolution
  → atlas lookup
  → quad/index topology
  → stable text packet
```

当字符串、字体和 atlas generation 不变时，只更新 transform、颜色和 opacity。

### 14.2 正式 FrameCompiler

让 Canvas 尽量保留 typed commands，由统一的 FrameCompiler 生成：

```text
DrawPacket {
    pipeline/material key
    stable geometry/topology
    dynamic attributes
    resource bindings
    clip/scissor
}
```

OpenGL、Vulkan 和 Software 再消费同一种 backend-neutral packet，减少各后端重复做相同展开。

当前 `DrawList` 已经承担部分 backend-neutral IR 职责，Vulkan 也能直接消费 compact solid 和 textured instances，但 shipping OpenGL onscreen path 仍保留自己的直接执行逻辑。下一步不是再增加一层抽象，而是明确哪一种 packet 是唯一事实来源，并让 fast path 不必在两个表示之间往返。

### 14.3 更细的计时

当前 `submit` 仍混合了 endFrame CPU、驱动调用和 GPU completion wait。后续应拆为：

```text
record_cpu
frame_compile_cpu
driver_submit_cpu
gpu_execution
gpu_completion_wait
```

OpenGL GPU 时间应使用延迟读取的 timer query，避免即时读取再次阻塞。

### 14.4 材质表或 shape ID

当前普通颜色虽已从 float4 压到 RGBA8，但仍按顶点重复。如果未来属性流再次成为瓶颈，可以评估：

```text
每顶点：position + coverage + 小型 shapeId
每 shape：color + transform + material 参数
```

在 OpenGL 3.3 中可通过 texture buffer 查询 per-shape 数据。不过它会增加 shader lookup 和缓存复杂度，只有数据证明 RGBA8 仍是瓶颈时才值得引入。

### 14.5 多批次 topology 复用与规模曲线

当前 OpenGL Renderer 只有一份 `pathBatchScratch_` 和一份 `pathBatchTopology_`。单批场景可以稳定复用，但超过 65,536 顶点后，不同批次会依次覆盖这份状态。后续应让缓存按稳定 batch identity 保存，或者把 topology packet 提前编译到 frame compiler。

参数化矩阵已经补上可扩展规模扫描。`standard` 使用 256/1024/4096 个图形，`thorough` 扩展到 64/256/1024/4096/16384，并分别覆盖：

```text
stable
dynamic-data
dynamic-structure
```

runner 已自动报告 draw reduction、record、submit、吞吐量和缩放曲线，NanoVG adapter 也已经实现相同参数语义。position、color、coverage 和 index 已共用一条帧上传流，删除了四条 GL buffer 每帧分别 orphan 的成本。仍未完成的是按稳定 batch identity 保留多批 topology、避免大规模动态几何在 flush 阶段重复变换和展开属性、把 topology hit/miss 加入公开报告，以及在更多 GPU/驱动上保存 standard/thorough 原始结果。

### 14.6 Vulkan 复杂图层

无滤镜 `saveLayer` 已删除全尺寸复制，稳定 clip mask 也已按内容签名跨帧复用，gradient clip 不再走 CPU readback。`clip_layers` 已从 31.34 ms 降至五进程中位数 8.80 ms。后续优化已从 P0 降为针对动态裁剪与滤镜组合的增量工作：

- 每层重复的 command/DrawingList 编译；
- 小 render target 的创建、复用和 layout transition；
- descriptor 与 sampled-image 绑定变化；
- 可以在同一 render pass 或同一提交中完成的相邻图层；
- 保持滤镜输入生命周期正确前提下的 filtered-layer copy 范围。

验收不应只看总时间，还必须保持 `clip_layers`、`frosted_glass`、`inner_shadow` 的像素 hash 稳定，并分别报告 filtered/unfiltered layer 路径。

## 15. 如何复现基础对比

准备 NanoVG checkout：

```powershell
git clone https://github.com/memononen/nanovg .nanovg
```

配置并构建两个 Release benchmark：

```powershell
cmake -S . -B build `
  -DWHATSCANVAS_BUILD_NANOVG_BENCHMARK_ADAPTER=ON `
  -DWHATSCANVAS_NANOVG_SOURCE_DIR="$PWD/.nanovg"

cmake --build build --config Release `
  --target WhatsCanvasPerformanceSuite WhatsCanvasNanoVGBenchmarkAdapter
```

执行标准合同：

```powershell
python scripts/cross_library_benchmark.py `
  --reference "whatscanvas=build/Release/WhatsCanvasPerformanceSuite.exe --backend opengl" `
  --adapter "nanovg=build/Release/WhatsCanvasNanoVGBenchmarkAdapter.exe --backend opengl" `
  --profile standard `
  --output-dir build/cross-library-nanovg
```

runner 会验证分辨率、profile、warmup、计时帧数、参数化 workload、字体 SHA-256、清屏与文字契约、GPU 同步模式和像素质量；默认自动启动 4 个 ABBA block，并保存每个进程的原始 JSONL、全部逐帧样本、PPM、汇总 JSON 和带 95% 置信区间的 Markdown。不要把同一长进程中的连续帧当作独立进程样本。

建议同时检查结果中的：

```text
total_median_ms
record_median_ms
submit_median_ms
draw_call_count
path_vertex_count
path_index_count
path_upload_count
path_upload_bytes
pixel_hash
```

只有当质量门、同步范围和结构指标都符合预期时，时间差才适合用于实现归因。

## 16. 总结

WhatsCanvas 与 NanoVG 的差距不是通过某个单一“大优化”消失的：

- Pass 1 删除重复缓存缺失、复制和 stream 覆盖；
- Pass 2 用 indexed AA 和 shared geometry 删除重复顶点与 payload copy；
- Pass 3 为简单 fill 建立 fast path，并使用 16 位索引；
- Pass 4 用参数化局部 mesh、RGBA8/coverage8 和 command pool 压紧数据；
- Pass 5 复用最终 merged packet、index topology 和 coverage，删除跨帧重建；
- 后续 Vulkan 提交让 indexed geometry、16 字节 compact solid vertex 和 short index 一直存活到 mapped frame upload；
- OpenGL/Vulkan glyph atlas 改用 Alpha8 和 48 字节 instance，删除四顶点展开；
- Vulkan frame slot 继续复用 descriptor、command buffer 和稳定提交结构。
- 参数化矩阵用规模、seed、数据变化和结构变化验证优化的适用范围；
- 哈希 LRU 和更合理的文本工作集容量将 1024 次动态结构文本从 61.49 ms 降到 7.65 ms；
- OpenGL 有序 8 槽多纹理批处理把 1024 次 dynamic-data 图片从 4.273 ms 降到 0.773 ms，draw 从 754 次降到 2 次，并让图片矩阵从 3/9 胜提升到 6/9 胜；
- OpenGL 路径属性与索引共用一条帧上传流，在不把 16 位索引扩成 32 位的前提下改善 4096 图形提交；
- Vulkan 普通图层直接采样离屏 render target，随后用内容签名复用稳定 clip mask、用双纹理 GPU 合成替换 gradient clip 的 CPU readback，将 `clip_layers` 从 41.38 ms 先降到 31.34 ms，再降到五进程中位数 8.80 ms，同时保持各阶段像素 hash 稳定。

最值得保留的结论是：

> 性能优化的核心不是更快地重复同一份工作，而是让系统知道哪些工作已经做过、哪些数据没有变化，并让最终可提交结果跨帧存活。

NanoVG 展示了连续数组和紧凑状态的价值；WhatsCanvas 在此基础上加入 indexed geometry、单 draw 合批、实例化和稳定 packet 复用，最终在动态几何合同的 Pass 5 配对样本中从慢 5.95 倍走到快 31.8%。固定合同仍展示三类主路径的竞争力，而最新 27 单元参数矩阵给出了更严格的边界：WhatsCanvas 12 胜、NanoVG 11 胜、4 项无明确胜负；图片已经解决普通多纹理退化，当前 P0 明确转向大规模和结构变化几何，而不是继续针对一个固定场景调参。

## 参考

- [跨库基准合同](CROSS_LIBRARY_BENCHMARKS.md)
- [性能基准与各轮结果](PERFORMANCE_BENCHMARKS.md)
- [性能优化 backlog 与验证记录](PERFORMANCE_OPTIMIZATION_TODO.md)
- [NanoVG 原始对比基线](../benchmarks/baselines/cross-library-nanovg-windows-i7-8700-gtx1060/README.md)
- `39f3d9a`：Pass 1，删除重复 text/path staging
- `3357371`：Pass 2，indexed AA 与 shared geometry
- `9520f52`：Pass 3，16 位索引与 simple-fill fast path
- `b287117`：Pass 4，参数化 primitive 与 packed attributes
- `342ab53`：Pass 5，跨帧复用稳定 OpenGL path batch
- `2cc2962`：packed path attribute 正确性加固
- `74676e7`：让 indexed path geometry 穿过 DrawList 并由 Vulkan 直接消费
- `5eda476`：Vulkan 合并几何直接写入统一 frame upload stream
- `20cf99c`：Vulkan 多 frame slot 与异步上传/提交
- `bc97a82`：Alpha8 glyph atlas 与 Vulkan compact glyph path
- `c6563f3`：glyph atlas batch 直接穿过 Renderer/Vulkan
- `1baed30`：OpenGL glyph atlas 实例化
- `e72b9d9`：Vulkan 16 字节 compact solid vertex、short index 与 direct mapped upload
- `4a6bf46`：Vulkan descriptor/command reuse 与统一圆角纹理实例批次
- `3f82973`：参数化性能矩阵、规模/seed/动态模式与多格式报告
- `58b5129`：文本哈希 LRU、布局坐标解耦与动态工作集修复
- `bb966ff`：Vulkan 无滤镜图层直接采样离屏 render target
- `cac08c1`：OpenGL 有序多纹理批处理、sampler object 与统一路径上传流
