# WhatsCanvas 性能评估

性能声明需要可复现的证据。因此，WhatsCanvas 将微基准测试、完整帧测量、渲染验证和环境元数据分离开来，而不是仅展示一个看起来有利的 FPS 数字。

## 评估层次

| 层级 | 目标 | 所回答的问题 |
| --- | --- | --- |
| CPU 微基准 | `WhatsCanvasCoreBenchmarks` | 哪个单独的布局、字体、路径、记录、上传或滤波操作发生了变化？ |
| 完整帧 | `WhatsCanvasPerformanceSuite` | 在 Software、OpenGL 或 Vulkan 上，一个代表性帧的耗时是多少？ |
| 聚焦滤镜 | `WhatsCanvasImageFilterBenchmarks` | 霜玻璃和内阴影的成本与像素工作量分别是多少？ |
| 证据 | JSONL + 像素哈希 + 内存 + 环境 | 其他开发者是否能够复现并审计该结果？ |

完整帧测试是主要的公开基准。较小的目标仍然有用，例如在帧回归需要定位时。

热点分析、优先实施待办和定量验收目标都记录在
[Performance Optimization TODO](PERFORMANCE_OPTIMIZATION_TODO.md) 中。

已检查入库的 [Windows i7-8700 / GTX 1060 参考运行](../benchmarks/baselines/cross-library-nanovg-windows-i7-8700-gtx1060/README.md)
展示了完整的报告格式，并保留了所有原始 JSONL 记录。
它是可复现的单机基线，而不是通用分数或跨库排名。该历史运行使用的是旧的 960 x 540、11 场景矩阵，因此它记录的是早期工作，并不与当前 1080p 套件在维度上兼容。

## 已验证的热点优化

已检查入库的参考运行还暴露出两个真实热点：圆角图像被路由到完整裁剪蒙版，而 Software/Vulkan 阴影处理的画布区域远大于阴影实际影响范围。优化后的实现使用原生的均匀圆角图像覆盖区域，裁剪阴影处理到扩展轮廓边界，保留 Vulkan 模糊在 GPU 上执行，延迟临时目标回收到 GPU 完成之后，并批量处理路径阴影轮廓通过。

以下候选项已于 2026-07-26 在完全相同的参考机器和驱动上重跑，构建类型为 `Release`，分辨率为 960 x 540，并使用 `standard` 30 + 5 帧配置。基线保留在仓库中，不会被覆盖，因此这些增量仍可审计。

| 后端 | 场景 | 参考中位数 | 优化后中位数 | 差异 |
| --- | --- | ---: | ---: | ---: |
| OpenGL | `image_grid` | 191.084 ms | 0.533 ms | -99.7% |
| Software | `image_grid` | 94.119 ms | 42.938 ms | -54.4% |
| Vulkan | `image_grid` | 47.344 ms | 0.290 ms | -99.4% |
| OpenGL | `shadow_grid` | 11.764 ms | 10.571 ms | -10.1% |
| Software | `shadow_grid` | 1003.717 ms | 70.411 ms | -93.0% |
| Vulkan | `shadow_grid` | 1129.557 ms | 14.947 ms | -98.7% |

这些结果并不说明 Vulkan 一般比 OpenGL 慢：在这次运行中，`image_grid` 在 Vulkan 上更快。剩余的 `shadow_grid` 差异，14.947 ms 对 10.571 ms，集中在 36 个小渲染目标和高斯滤波任务上。在这个 GTX 1060 驱动下，它们的 Vulkan 渲染通道、描述符和命令缓冲区固定成本比 OpenGL 驱动内部调度更明显。

Software 和 OpenGL 阴影哈希保持不变。图像哈希在圆角边缘处有意发生变化，因为剪裁蒙版光栅化被原生着色器覆盖所替代。Vulkan 阴影输出在 CPU 模糊被 GPU 路径替换时发生变化；其与 OpenGL 的比较中，最大通道差异为 1。优化后的修订版本已通过所有 64 个 Release CTest 条目，包括 22 个 Vulkan 测试、Software 金像、OpenGL/Vulkan 滤镜像素一致性、文本、Unicode、示例、API 文档和已安装包消费者。

## 已验证的 1080p 压力优化

1080p 压力场景暴露出第二类隐藏在小场景中的瓶颈：OpenGL 在每次 flush 时都会重建 sprite GPU 对象，文本每个 atlas glyph 记录一条命令，重复的平移形状会重建相同的网格，而 Software 光栅化对简单四边形和统一抗锯齿内部区域支付了三角形插值成本。

候选项于 2026-07-27 在同一台 Windows i7-8700 / GTX 1060 机器上以 `Release` 构建测量。每个场景都在新的进程中运行，分辨率为 1920 x 1080。标准配置使用 30 个计时帧和 5 个 warmup 帧；波动更大的 Software 文本结果使用更彻底的 120 + 20 配置。

| 后端 | 场景 | 优化前中位数 | 优化后中位数 | 差异 | 命令 / 绘制 |
| --- | --- | ---: | ---: | ---: | ---: |
| OpenGL | `geometry_stress` | 43.890 ms | 24.759 ms | -43.6% | 2,305 / 2,305 |
| OpenGL | `text_stress` | 893.640 ms | 13.459 ms | -98.5% | 577 / 194 |
| Vulkan | `geometry_stress` | 50.940 ms | 25.990 ms | -49.0% | 2,305 / 1 |
| Vulkan | `text_stress` | 34.910 ms | 16.134 ms | -53.8% | 577 / 577 |
| Software | `geometry_stress` | 157.090 ms | 103.599 ms | -34.1% | 2,305 / 2,305 |
| Software | `text_stress` | 79.960 ms | 60.348 ms | -24.5% | 577 / 577 |

OpenGL 现在会在不同帧之间保留一个 sprite batch 的程序、VAO 和缓冲区。
共享 atlas 和渲染状态的 glyph runs 使用一个紧凑的 image-batch 命令。
Vulkan 将兼容的实心几何体降至一个 primitive，而不丢失每顶点颜色或解析 AA 覆盖。平移归一化填充和 AA 网格在字节受限的 LRU 缓存中复用，Software 则针对轴对齐图像四边形和统一内部三角形提供直接光栅路径。

OpenGL 和 Vulkan 的验证哈希在这些优化中保持稳定。Software 的归一化平移几何和直接四边形插值可能与此前的双三角形运算存在最多 1 个通道值差异；测量对比中仅有 0.001543% 的像素发生变化，最大通道差异为 1，平均通道差异为 0.000005。文本输出在 Software、OpenGL 和 Vulkan 之间收敛到相同的验证哈希。

后续批处理阶段将 OpenGL 的 `geometry_stress` 从 2,305 个 GPU 绘制减少到 9 个，方法是把兼容的 2D 仿射变换展平为有界的每顶点批处理。该过程期间观察到的标准中位数范围为 19.777 ms 到 25.565 ms，之前的 24.759 ms 结果处于这个范围内，因此绘制数减少已得到验证，但没有宣称稳定的帧时改进。像素哈希仍为 `44121eb5a074425f`。

Vulkan 现在会使用打包 RGBA8 每顶点 tint 合并兼容的 atlas/image 四边形，并在一帧内复用相同的采样图像描述符集。
`text_stress` 从 577 个 GPU 绘制下降到 194，同时保留像素哈希 `6554c1da7b50ade0`。它最终的串行标准中位数为 16.320 ms，而此前为 16.134 ms，差异 1.2%，处于运行变异范围内；这同样属于结构化批处理改进，而不是计时加速声明。

## 已验证的 Android compositing 优化（v0.9.0）

真机 Android 上，`compositing_stress` 存在两项桌面 1080p 矩阵未覆盖的 CPU 瓶颈：裁剪版 `saveLayer` 的离屏重放绕开了共享批处理路径，会为每条排队的绘制单独提交一条 OpenGL 命令；而每次 restore 又会在执行 backdrop 滤镜链之前，把 pre-layer 命令序列再走一次 `FrameCompiler.compile`。两者合计吃掉两台真机上每帧约 300 ms 的 `recordCpu`，把 FPS 压到个位数。

测量于 2026-08-23 使用 Android profile APK 完成。两台设备使用同一个 APK、同一段 warmup、相同 profile 构建选项、相同动画时钟，全程 thermal status = 0。

| 设备                 | 指标                  |    v0.8.1 |    v0.9.0 |    变化   |
| ---                  | ---                   |     ---:  |     ---:  |     ---:  |
| Mi MIX 2（1080x2160）| FPS                   |       4.7 |      30.8 |    +6.6x  |
| Mi MIX 2             | recordCpu / 帧        |  18-20 ms |    7.4 ms | ~63% 下降 |
| Mi MIX 2             | frameCompile / 帧     |   8-9 ms  |   0.24 ms | ~97% 下降 |
| Mi MIX 2             | saveLayer backdrop    |     13 ms |      0 ms | 命中时    |
| Mi MIX 2             | draws                 |        45 |        28 |           |
| U Ultra（1440x2560） | FPS                   |       3.2 |      26.4 |    +8.3x  |
| U Ultra              | recordCpu / 帧        |  18-20 ms |    5.1 ms | ~72% 下降 |
| U Ultra              | frameCompile / 帧     |   5-6 ms  |   0.10 ms | ~98% 下降 |
| U Ultra              | saveLayer backdrop    |     15 ms |      0 ms | 命中时    |
| U Ultra              | draws                 |        45 |        28 |           |

固定动画时间戳（`capture_time_seconds=3.0`）下的像素对齐已核验。v0.8.1 无 cache 基线与 v0.9.0 cache 命中结果的 SHA-256 完全一致：
`BCB565C9F7A009AE5DFD0D3FD4D6C9F0C023A69538DDE33BF07647638EFB5BDE`。cache 命中直接复用上一帧的 filtered backdrop image，可见输出零差异。其它场景无回归：`feature_showcase` 59.8 (Mi) / 54.6 (U)，`text_stress` 57.9 (Mi) / 34.6 (U)，`geometry_stress` 59.6 (Mi) / 55.5 (U)。

关键改动同步记录在 `CHANGELOG.md` 的 `[0.9.0]` 段：

- 裁剪版 `saveLayer` 的离屏路径改走与主 framebuffer 相同的 FrameCompiler 与 OpenGL sprite/path 批处理路径。`DrawPathProgram` 的 VAO、program、投影 uniform，以及设备生命周期的 `SpriteBatch` 与 GL programs 会在离屏重放之间复用。
- 新引入的 backdrop 编译结果缓存以 `(pre-layer 命令 fingerprint, backdrop 滤镜链 fingerprint, canvas + layer 几何)` 为 key，一旦匹配就同时短路 `renderQueuedCommandsToImageResource()` 与 `filterImageResource()`。命中率与诊断字段通过 `Canvas::RenderStats` 暴露：`backdropCacheHits`、`backdropCacheMisses`、`backdropFingerprintStableFrames`、`backdropFingerprintDivergentFrames`、`backdropFingerprintUncacheable`、`backdropFingerprintCpuTimeNs`、`backdropFirstDivergentIndex`、`backdropFirstDivergentType`、`backdropFirstDivergentReason`。
- `ScissorState` 是 `{ bool + 4 int }`，编译器会在 `enabled` 后插入 3 个 padding 字节。直接对整个结构体做 `mixBytes` 会把未初始化的 padding 折进 fingerprint，导致 cache 每帧都 miss 却没有可见症状。修复是逐字段显式 hash。任何在代码库里做内容 hash 的 POD，只要混用 `bool`/`enum(1 字节)` 与 `int`/pointer 成员，都必须逐字段 hash，否则 cache 会静默失效。

两台设备的下一个 CPU 优化目标是 U Ultra 上的 `text_stress`（34.6 FPS）。该帧 `commands=233`、`draws=206`、`batchBreak=14/6/0/0`：14 次批处理在 command type 边界断开，6 次在渲染状态边界断开。当前 `Renderer::reorderIndependentPathRuns()` 已经处理独立的 solid fill Path 段，但 `finishSegment()` 遇到任何非 Path 命令都会立刻收段，因此 `drawTextScene()` 里 Path 与 Text 命令交错时，被 Text 分隔的 Path 段永远不会被跨 Text 重排。下一次优化的计划见 `PERFORMANCE_OPTIMIZATION_TODO.md`：先给 14 次 command-type 断点加 `(lhs.type, rhs.type)` pair 分类诊断，再决定是否让 reorder 段跨越那些设备空间边界与 Path 段不重叠的非 Path 命令。

## 标准场景矩阵

默认分辨率为 1920 x 1080。每个场景都是确定性的，并在计时结束后产生固定的验证帧哈希。默认值匹配广泛部署的桌面显示工作负载，而 `--width` 和 `--height` 仍可用于受控研究。

| 场景 | 覆盖范围 | 缓存模式 | 每帧操作数 |
| --- | --- | --- | ---: |
| `solid_rects` | 密集填充矩形和命令提交 | churn | 576 |
| `rounded_ui` | 圆角 UI 表面和抗锯齿边缘 | churn | 120 |
| `path_cached` | 重复的复杂路径几何 | hot | 160 |
| `path_churn` | 每帧构建和细分路径 | churn | 160 |
| `geometry_stress` | 2,304 个混合矩形、圆角矩形、圆、椭圆和自定义路径 | churn | 2,304 |
| `image_grid` | 重复使用 RGBA 纹理缩放和采样 | hot | 96 |
| `clip_layers` | 嵌套裁剪、变换和图层 | churn | 144 |
| `shadow_grid` | 不同半径的形状阴影 | churn | 36 |
| `text_cached` | 重复的形状文本和 glyph-atlas 复用 | hot | 120 |
| `text_churn` | 改变文本内容和 glyph 查找压力 | churn | 120 |
| `text_stress` | 576 个多语言文本调用，扩展为约 8,000 个缓存 glyph 命令 | hot | 576 |
| `contract_text_latin` | 576 个固定 Roboto Latin 调用，用于跨库比较 | hot | 576 |
| `frosted_glass` | 背景捕获、模糊和图层合成 | hot | 4 |
| `inner_shadow` | 带内阴影的过滤控件 | hot | 24 |

Hot 和 churn 变体都是刻意设计的。渲染器应该同时展示稳定态缓存效率和内容变化成本。压力场景也刻意足够大，以暴露命令记录、glyph-atlas 切换、批处理、细分和提交瓶颈，而小型 UI 样本可能会掩盖这些问题。

## 参数化工作负载矩阵

固定场景对回归锚点很有用，但单一对象数量可能被过拟合。`geometry_stress`、`image_grid` 和 `text_stress` 因此也提供了可选的参数化路径。除非提供工作负载选项，否则它们的默认行为和历史像素哈希保持不变。

| 选项 | 含义 |
| --- | --- |
| `--workload stable` | 几何体、资源和命令拓扑在帧之间保持稳定 |
| `--workload dynamic-data` | 位置、颜色和文本选择发生变化，但拓扑保持稳定 |
| `--workload dynamic-structure` | 原始体、纹理、文本和渲染状态选择可能在每帧变化 |
| `--operations N` | 所选压力场景中的绘制操作数 |
| `--seed N` | 记录在 JSONL 中的确定性内容种子 |
| `--texture-count N` | 图像资源基数，从 1 到 256 |
| `--rounded-ratio 0..1` | 使用圆角覆盖的图像操作比例 |
| `--state-change-rate 0..1` | 确定性地改变混合状态的操作比例 |
| `--text-length N` | 每个生成文本样本中的 ASCII 字符数 |

矩阵运行器会在全新的 Release 进程中启动每个工作负载和 seed，然后报告进程中位数的中位数。它保留原始 JSONL，并写出 JSON、CSV、Markdown 和每后端的 log-log SVG 缩放图表：

```powershell
python scripts\run_benchmark_matrix.py `
  --executable build\Release\WhatsCanvasPerformanceSuite.exe `
  --backend opengl --backend vulkan `
  --preset standard --profile standard `
  --output-dir build\performance-matrix
```

`smoke` 预设覆盖三大类别以及稳定/结构变化路径，只用一个 seed。`standard` 使用三种操作规模、所有三种工作负载模式和三个 seed。`thorough` 扩大规模范围，并使用五个 seed。使用 `--dry-run` 可在长时间测量前审核每个命令。

一个矩阵案例仍然可以直接复现：

```powershell
build\Release\WhatsCanvasPerformanceSuite.exe `
  --backend vulkan --profile standard --scene image_grid `
  --workload dynamic-structure --operations 1024 --seed 2003 `
  --texture-count 32 --rounded-ratio 0.5 `
  --state-change-rate 0.125 `
  --output build\image-grid-dynamic.jsonl
```

报告包含尺度、动态模式、纹理基数、圆角比例、状态变化率、文本长度、进程范围、同步帧时序、吞吐量、绘制调用和绘制减少量。这使稳定缓存收益可见，同时不掩盖动态拓扑或高状态变更成本。

原生参数化矩阵评估 WhatsCanvas 后端和版本。对于实现相同参数语义的外部库，跨库矩阵运行器会把像素质量契约和独立 ABBA 对比应用到每个矩阵单元：

```powershell
python scripts\run_cross_library_matrix.py `
  --reference "whatscanvas=build/Release/WhatsCanvasPerformanceSuite.exe --backend opengl" `
  --adapter "nanovg=build/Release/WhatsCanvasNanoVGBenchmarkAdapter.exe --backend opengl" `
  --preset standard --profile standard `
  --repetitions 4 `
  --output-dir build\cross-library-matrix
```

`standard` 包含 27 个单元：三种场景、三种操作规模和三种变化模式。其默认工作负载 seed 为 `1001`；使用 `--seeds 1001,2003,3001` 可测试内容敏感度。ABBA 重复控制进程噪声，独立于内容多样性。默认的两块 ABBA 使用每个渲染器每个单元四个新进程；使用 `--repetitions 8` 可以生成可发表的四块运行。运行器会保留每个单元的原始 JSONL 和捕获，然后写出聚合 JSON、CSV 和 Markdown，并包含质量状态和 95% 置信区间判定。

首次检查入库的 Windows i7-8700 / GTX 1060 运行通过了所有 27 个质量门控。每个单元有两块 ABBA，WhatsCanvas OpenGL 在 12 个单元中明显更快，NanoVG GL3 在 12 个单元中更快，3 个跨越了配对比率 95% 置信边界。分解为：几何 2/9 个 WhatsCanvas 胜出、图像 3/9、文本 7/9。稳定的单纹理图像和大多数文本工作负载偏向 WhatsCanvas；动态多纹理/状态图像流和中/高规模动态几何更偏向 NanoVG。查看
[原始参数矩阵基线](../benchmarks/baselines/nanovg-win-i7-8700-gtx1060/README.md)
可看到每个单元及其 216 个进程 JSONL 记录。

### 优化后参数矩阵（`cac08c1`）

在有序八槽 OpenGL 多纹理批处理、专用批处理采样器和一个共享路径属性/索引上传流之后，重新运行了同一 27 单元标准矩阵。每个单元使用两块 ABBA 和每个渲染器四个新进程，27 个质量门都通过了。WhatsCanvas 赢得 12 个单元，NanoVG 赢得 11 个，4 个不确定：

| 分类 | WhatsCanvas 更快 | NanoVG 更快 | 不确定 |
| --- | ---: | ---: | ---: |
| 几何 | 2 | 6 | 1 |
| 图像 | 6 | 3 | 0 |
| 文本 | 4 | 2 | 3 |
| **总计** | **12** | **11** | **4** |

图像结果发生了实质变化，而不是停留在噪声内。三种稳定图像和三种动态数据图像单元现在都偏向 WhatsCanvas。在 1,024 操作下，动态数据图像从 4.273 ms 下降到 0.773 ms，而 NanoVG 测得 1.667 ms。剩余的确定性 NanoVG 胜出是：

| 场景 | 模式 | 操作数 | WhatsCanvas | NanoVG | WhatsCanvas 差距 |
| --- | --- | ---: | ---: | ---: | ---: |
| `geometry_stress` | dynamic-structure | 256 | 1.254 ms | 0.748 ms | 慢 65.8% |
| `geometry_stress` | dynamic-data | 1,024 | 1.890 ms | 1.575 ms | 慢 20.1% |
| `geometry_stress` | dynamic-structure | 1,024 | 2.912 ms | 1.661 ms | 慢 72.4% |
| `geometry_stress` | stable | 4,096 | 6.619 ms | 4.542 ms | 慢 44.8% |
| `geometry_stress` | dynamic-data | 4,096 | 6.636 ms | 4.711 ms | 慢 38.8% |
| `geometry_stress` | dynamic-structure | 4,096 | 9.425 ms | 5.647 ms | 慢 65.6% |
| `image_grid` | dynamic-structure | 64 | 0.494 ms | 0.411 ms | 慢 21.0% |
| `image_grid` | dynamic-structure | 256 | 0.997 ms | 0.779 ms | 慢 28.6% |
| `image_grid` | dynamic-structure | 1,024 | 2.338 ms | 1.652 ms | 慢 37.6% |
| `contract_text_latin` | stable | 1,024 | 5.905 ms | 5.874 ms | 慢 0.7% |
| `contract_text_latin` | dynamic-data | 1,024 | 6.178 ms | 5.898 ms | 慢 4.5% |

这改变了优化优先级。大规模且结构变化的几何体是主导差距。动态结构图像次之：它们有意的混合屏障和变化的纹理集仍然会产生许多小批次，即使普通多纹理流不再如此。两个文本损失都较小、优先级较低；1,024 操作的 dynamic-structure 文本单元仍偏向 WhatsCanvas，6.652 ms 对 8.794 ms。

渲染器计数器更精确地定位了这些差距：

| 工作负载 | 记录 | 提交 | 绘制 | 结构性证据 |
| --- | ---: | ---: | ---: | --- |
| Geometry dynamic-structure, 256 | 0.180 ms | 1.050 ms | 46 | 状态/拓扑变化已经使小帧碎片化 |
| Geometry dynamic-structure, 1,024 | 0.680 ms | 2.250 ms | 237 | 编译和小提交规模都在扩展 |
| Geometry dynamic-structure, 4,096 | 2.810 ms | 7.010 ms | 950 | 97,636 个顶点和约 2.1 MiB 的路径上传 |
| Geometry stable, 4,096 | 2.730 ms | 4.280 ms | 2 | 约 2.0 MiB 上传：绘制数不是主要成本 |
| Images dynamic-structure, 1,024 | 0.310 ms | 2.060 ms | 283 | 真正的混合屏障在多纹理批处理后主导提交 |
| Text dynamic-data, 1,024 | 4.140 ms | 2.080 ms | 2 | 剩余文本差距主要在记录侧工作 |

这些计数器来自同一矩阵中的代表性 WhatsCanvas 进程，它们是诊断信息，不是额外的独立样本。它们排除了一个泛化修复：几何体需要更少的帧编译和属性扩展，图像 dynamic-structure 需要更便宜的屏障受限批次，而高计数文本需要记录路径分析，而非另一个绘制调用优化。

### 当前参数矩阵

在多包拓扑复用、用于大仿射批处理的 GPU 形状参数、紧凑按需路径梯度存储、持久化 OpenGL sprite-sequence 状态、短路径分配清理和冗余 GL 状态移除之后，重新运行了标准矩阵。同样的两块 ABBA 计划和每个渲染器四个新进程也应用到了每个单元上。所有 27 个质量门都通过：

| 分类 | WhatsCanvas 更快 | NanoVG 更快 | 不确定 |
| --- | ---: | ---: | ---: |
| 几何 | 8 | 0 | 1 |
| 图像 | 9 | 0 | 0 |
| 文本 | 9 | 0 | 0 |
| **总计** | **26** | **0** | **1** |

这次运行关闭了此前所有确定性的图像、文本和几何损失。重用 sprite 程序、VAO、投影、采样器 uniform、采样器绑定和不变的纹理槽位，使即使刻意碎片化的图像工作负载也能在不重排透明绘制的情况下保持竞争力：

| 场景 | 模式 | 操作数 | WhatsCanvas | NanoVG | 结果 |
| --- | --- | ---: | ---: | ---: | --- |
| `geometry_stress` | stable | 4,096 | **3.971 ms** | 5.417 ms | WhatsCanvas 快 26.7% |
| `geometry_stress` | dynamic-data | 4,096 | **3.893 ms** | 5.175 ms | WhatsCanvas 快 24.8% |
| `image_grid` | dynamic-structure | 1,024 | **1.328 ms** | 1.869 ms | WhatsCanvas 快 28.9% |
| `contract_text_latin` | dynamic-structure | 1,024 | **6.886 ms** | 10.132 ms | WhatsCanvas 快 32.0% |

此前较慢的两个几何单元现在也偏向 WhatsCanvas：

| 模式 | 操作数 | WhatsCanvas | NanoVG | 结果 |
| --- | ---: | ---: | ---: | ---: |
| `dynamic-structure` | 1,024 | **1.730 ms** | 1.906 ms | WhatsCanvas 快 9.2% |
| `dynamic-structure` | 4,096 | **5.751 ms** | 6.029 ms | WhatsCanvas 快 4.6% |

在 256 操作下，同一工作负载仍然统计上不确定（0.788 vs 0.811 ms）。实现不会削弱 AA、重排混合屏障，或根据基准大小分支。它移除了通用短路径成本：`Path` 预留常见的紧凑 verb 数量，轮廓提取移动存储并按已知 verb 数量预留，简单填充直接消费已解析轮廓。OpenGL 也会缓存不变的加法混合方程，并在剪裁/裁剪状态已为空时立即返回。

## 配置文件

| 配置文件 | 计时帧数 | warmup 帧数 | 预期用途 |
| --- | ---: | ---: | --- |
| `quick` | 3 | 1 | 构建/模式/回读烟雾测试 |
| `standard` | 30 | 5 | 正常本地比较和参考报告 |
| `thorough` | 120 | 20 | 专用硬件上的稳定发布调查 |

每个场景还记录一个 pre-warmup cold frame。对于正式运行，提供的运行器会在每个场景中启动新的进程。这可以防止前一个文本场景的 glyph atlas、前一个路径场景的缓存，或进程峰值 RSS 改变下一个场景的结果。

## 指标

- `record_*_ms`: `beginFrame` 加上公开 Canvas 绘制调用的时间。
- `end_frame_cpu_*_ms`: 在基准测试显式后端完成等待前，CPU 在 `Canvas::endFrame()` 内花费的墙钟时间。
- `gpu_wait_*_ms`: 基准测试完成屏障所消耗的时间。OpenGL 使用 `glFinish`；Vulkan 等待其队列。这是阻塞式基准诊断，而不是正常呈现路径。
- `submit_*_ms`: 向后兼容的 `end_frame_cpu_*_ms` 和 `gpu_wait_*_ms` 之和。
- `total_*_ms`: 完整同步帧时间。
- `median`、`p90`、`p95`、`p99`、`mean`、`min`、`max`、`stddev`、`cv`: 测量帧的分布统计量。
- `cold_total_ms`: warmup 之前的第一帧。
- `fps`: `1000 / total_median_ms`；这是吞吐量，而不是显示刷新率。
- `operations_per_second`: 声明的场景操作数除以中位数时间。
- `readback_ms`: 单独计时的 RGBA 回读，不计入帧时间。
- `pixel_hash`: 固定帧 RGBA 哈希，用于检测缺失或已变化输出。
- `rss_*`、`peak_rss_bytes`、`private_or_virtual_bytes`: 进程内存观察值。最后一个字段采用最接近的可移植 OS 计数器，且刻意不被展示为在所有平台上都等同于私有内存语义。
- `command_count`、`draw_call_count`、cache bytes、filter/pass/pixel counts，以及 render-target 统计：公开 `Canvas::RenderStats` 诊断信息。
- `image_batch_quad_count`、`image_batch_instanced_quad_count` 和 `image_batch_upload_bytes`: SpriteBatch 输入四边形、由一个 12-float GPU 实例表示的子集，而不是四个 14-float 顶点，以及对应的每帧顶点/实例上传流量。
- `flush_cpu_ns`: 渲染器 flush 的墙钟时间。`frame_compile_cpu_ns` 隔离命令到 packet 降低的成本（当启用 `FrameCompiler` 路径时），而 `device_execution_cpu_ns` 隔离设备命令执行。
- `gpu_time_available` 和 `gpu_time_ns`: 延迟的、非阻塞后端计时器结果。OpenGL 使用三查询环；不支持的后端报告 `false`，而不是替换为 CPU 时间。GPU 计时默认关闭；运行诊断时可传 `--gpu-timing`，这样比较基线不会额外支付计时查询开销。
- `compiled_packet_count`、`compiled_vertex_bytes` 和 `compiled_index_bytes`: 提交的紧凑 packet 占用空间。
- `text_normalization_count`、shape/layout cache hits 和 misses、`text_layout_view_hits`（缓存命中而不复制缓存四边形向量）、`glyph_atlas_hits/misses`、`glyph_rasterization_count`、`zero_area_glyph_hits`、`generated_glyph_quad_count` 和 `glyph_atlas_dirty_bytes`: 每帧可移植文本管线工作量。原生文本后端可能对隐藏在平台 API 后的阶段报告 0。
- `text_normalization_cpu_ns`、`text_layout_cache_cpu_ns`、`text_shaping_cpu_ns`、`glyph_cache_lookup_cpu_ns`、`glyph_raster_cpu_ns` 和 `glyph_atlas_upload_cpu_ns`: 主要可移植文本阶段的 CPU 时间分解。`text_bidi_cpu_ns`、`text_font_fallback_cpu_ns`、`text_font_data_cpu_ns` 和 `text_shape_engine_cpu_ns` 细分整形，以免将 provider 匹配误认为 HarfBuzz/simple-shaper 执行。
- `path_input_vertex_count`、`path_tessellated_vertex_count`、`path_aa_expanded_vertex_count`、`path_merged_vertex_count` 和 `path_uploaded_vertex_count`: 每帧路径几何体流。AA 值是索引后唯一顶点计数，因此可能低于预索引三角形汤。Merged 也保留在历史的 `path_vertex_count` 名称下。上传计数表示传输到活动后端的记录位置；使用字节计数器分析总属性带宽。
- `command_object_count`、`command_allocation_count` 和 `command_pool_reuse_count`: 区分逻辑命令构建和真实系统堆流量。高对象计数但为 0 分配意味着命令池在工作，并不能证明还有另一个分配优化。
- `command_clone_count`、`payload_copy_bytes` 和 `staging_capacity_bytes`: 保留的 Picture 克隆、已知 CPU 载荷物化/复制流量，以及保留可复用 staging 容量。复制字节是诊断性下界，而不是总进程内存带宽。Staging 还包括 queued image-batch quad 容量和 renderer/device staging；它不是 arena 计量，因为 WhatsCanvas 还没有统一的 frame arena。
- `batch_break_command_type_count`、`batch_break_state_count`、`batch_break_texture_limit_count` 和 `batch_break_vertex_limit_count`: 原本连续的 OpenGL 路径/sprite 序列停止批处理的原因。自然结束帧终止不计入中断。
- `tracked_resource_bytes`: WhatsCanvas 拥有的 glyph-atlas、池化渲染目标、细分、stroke 和 bitmap-text cache 字节总和。

像素相等并不等于感知质量分数。一个快速的空白帧可能因错误原因很快，所以每个结果必须具备非空回读和稳定哈希，而视觉回归仍然是单独的质量门控。

## 构建与运行

使用 `Release`；Debug 结果不适合性能声明。

```powershell
cmake -S . -B build -DWHATSCANVAS_BUILD_BENCHMARKS=ON
cmake --build build --config Release --target WhatsCanvasPerformanceSuite
.\scripts\run_performance_suite.ps1 `
  -Profile standard `
  -Backends software,opengl,vulkan `
  -OutputDir build/performance-results
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DWHATSCANVAS_BUILD_BENCHMARKS=ON
cmake --build build --target WhatsCanvasPerformanceSuite
PROFILE=standard BACKENDS="software opengl vulkan" \
  OUTPUT_DIR=build/performance-results \
  ./scripts/run_performance_suite.sh
```

运行器默认使用 Software，因为它在每个支持的宿主上都可用。只请求当前构建中编译进的 GPU 后端。若要直接运行单个案例：

```powershell
build\Release\WhatsCanvasPerformanceSuite.exe `
  --backend opengl --profile standard --scene text_cached `
  --output build\text-cached.jsonl `
  --capture-dir build\performance-captures
```

使用 `--list-scenes` 查看场景名称。`--frames`、`--warmup`、`--width` 和 `--height` 可覆盖配置用于研究。工作负载选项会记录在每个 JSONL 结果中，但自定义设置不应混入固定场景的标准参考报告；参数化发布应使用矩阵运行器。

## 对比修订版

在同一空闲机器上、使用相同驱动、功耗模式、构建类型、后端、分辨率和配置文件，捕获基线与候选结果：

```powershell
python scripts\compare_performance.py `
  build\perf-baseline build\perf-candidate `
  --regression-threshold 10 `
  --output build\performance-comparison.md
```

可选门控：

```powershell
python scripts\compare_performance.py `
  build\perf-baseline build\perf-candidate `
  --regression-threshold 10 `
  --fail-on-regression --fail-on-hash-change
```

比较工具会匹配后端、场景、分辨率和配置文件；报告中位数、p95、峰值 RSS 和哈希变化。它会在机器、驱动、编译器、构建、分辨率或采样设置不同的时候拒绝给出时间判定。`--allow-incompatible` 仅用于显式探索性报告。原始文件也可以独立检查：

```sh
python3 scripts/compare_performance.py --validate build/performance-results
```

单个运行目录可以直接转成可审阅的 Markdown 报告，而无需手动选择或复制数字：

```sh
python3 scripts/compare_performance.py \
  --summary build/performance-results \
  --output build/performance-summary.md
```

## 跨库比较

可执行程序
[`cross_library_benchmark.py`](../scripts/cross_library_benchmark.py) 运行器和
机器可读的 [`contract.json`](../benchmarks/cross_library/contract.json) 在比较同步完整帧时间前会验证外部适配器。有关适配器 CLI、固定资产、所需 JSONL 元数据、质量阈值和发布规则，请参阅 [cross-library benchmark contract](CROSS_LIBRARY_BENCHMARKS.md)。

一个公平的适配器必须在相同分辨率下渲染相同场景，且具有等价的抗锯齿、裁剪、混合、文本、采样和同步。它必须发布：

- 适配器源码和精确依赖修订版本；
- 优化的编译器标志和后端/设备元数据；
- warmup 和样本数量；
- 原始 JSONL、输出图像，以及不支持操作的描述；
- 质量证据和计时数据，且不能悄悄简化场景。

不要在不同库之间直接比较原始绘制调用计数，除非该操作具有相同语义。不要将异步 GPU 提交与同步完整帧时间直接比较。当某个效果没有等价实现时，应标记该场景为 unsupported，而不是替换成更容易的效果。

首次质量门控的 NanoVG GL3 基线现已检查入库。在 Windows i7-8700 / GTX 1060 机器上，三次独立 1080p Standard 进程中位数的中位数为：

| 场景 | WhatsCanvas OpenGL | NanoVG GL3 | 结果 |
| --- | ---: | ---: | --- |
| `geometry_stress` | 25.659 ms | 4.316 ms | NanoVG 快 5.95x |
| `image_grid` | 0.308 ms | 0.372 ms | WhatsCanvas 快 1.21x |
| `contract_text_latin` | 15.911 ms | 3.334 ms | NanoVG 快 4.77x |

所有三个 NanoVG 捕获都通过了其场景质量门控。这暴露出一个真实的 WhatsCanvas 弱点：每帧路径构建/细分和文本记录；图像批处理路径已经具有竞争力。见 [原始基线和方法论](../benchmarks/baselines/cross-library-nanovg-windows-i7-8700-gtx1060/README.md)。

第一次优化后已重新测量，且在相同机器上进行了五次独立进程测试。进程中位数的中位数为：

| 场景 | 优化前 | 优化后 | 改进 | NanoVG GL3 | 剩余差距 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 15.73 ms | 38.7% | 4.316 ms | 3.64x |
| `image_grid` | 0.308 ms | 0.29 ms | 在噪声内 | 0.372 ms | 无法证明 |
| `contract_text_latin` | 15.911 ms | 4.78 ms | 70.0% | 3.334 ms | 1.43x |

每个进程都产生了相同的场景哈希。文本改进来自已解决的 glyph-layout 缓存、零面积 glyph 缓存和四顶点索引 sprite quads。几何改进来自 move-only 命令交接、append-only 每帧路径流和确定性 AA 缓存抖动移除。几何体仍然将简单原始体扩展为通用三角形汤路径，因此其剩余差距需要在 [performance optimization backlog](PERFORMANCE_OPTIMIZATION_TODO.md) 中跟踪的语义原始体和索引 AA 工作。

第二轮几何优化保留了相同质量契约，并再次使用五次独立进程中位数：

| 场景 | 原始 | 第 1 轮 | 第 2 轮 | NanoVG GL3 | 第 2 轮差距 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 15.73 ms | 8.68 ms | 4.316 ms | 2.01x |
| `image_grid` | 0.308 ms | 0.29 ms | 0.31 ms | 0.372 ms | 在噪声内 |
| `contract_text_latin` | 15.911 ms | 4.78 ms | 4.42 ms | 3.334 ms | 1.33x |

索引 AA 将几何体流从 269,598 个重复顶点降到 62,984 个顶点加索引。不可变共享缓存几何移除了重复的每命令 vector 拷贝，预先分配仿射批处理装配减少提交工作。场景现在只用一个 draw，并上传 2,841,944 字节路径，而不是 7,548,744。捕获哈希保持为 geometry、image、text 的 `44121eb5a074425f`、`432ad28b33a51375` 和 `737cad1b0d1169f2`。

第三轮优化增加了 16 位合并索引 packet 和受保护的简单实心填充路径。五进程几何测量和九进程文本检查产生：

| 场景 | 原始 | 第 2 轮 | 第 3 轮 | NanoVG GL3 | 第 3 轮差距 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 8.68 ms | 6.45 ms | 4.316 ms | 1.49x |
| `image_grid` | 0.308 ms | 0.31 ms | 0.27 ms | 0.372 ms | 在噪声内 |
| `contract_text_latin` | 15.911 ms | 4.42 ms | 4.81 ms | 3.334 ms | 1.44x |

几何场景记录耗时 3.07 ms，提交耗时 3.40 ms。其 269,598 个索引占用 539,196 字节，确认采用 16-bit 流；总路径上传为 2,302,748 字节，较原始版本下降 69.5%。复杂几何语义仍保留通用路径管线，并且所有捕获保留此前哈希。

第四轮几何优化引入了参数化局部空间原始体网格、归一化 RGBA8/coverage8 属性用于合并实心 packet、受限 thread-local 路径命令池，以及 Release trusted-index 快速路径。七轮交替的 WhatsCanvas/NanoVG 进程产生：

| 场景 | 原始 | 第 3 轮 | 第 4 轮 | 配对 NanoVG GL3 | 第 4 轮差距 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 6.45 ms | 4.682 ms | 3.965 ms | 1.18x |

当前记录/提交中位数为 1.767/2.956 ms。该场景仍使用一个 draw、62,984 个顶点和 269,598 个 16-bit 索引，但总路径上传现在为 1,357,988 字节：较第 3 轮下降 41.0%，较原始版下降 82.0%。所有七个进程均产生 `5e7e67fb8b9ca579`。

参数化曲线缓存只相对第 3 轮改变了 171/2,073,600 像素（0.0082%）。每个改变的通道都是一个 8-bit 水平，RMSE 0.0069。完整 Release 构建和所有 66 个 Release 测试均通过。

第五轮几何优化保留了跨帧的合并路径 packet，复用了稳定共享几何索引拓扑和打包 coverage stream，并从简单实心填充记录路径中移除了完整 `Paint` 拷贝和通用 4 x 4 矩阵乘法。八个 WhatsCanvas 和八个 NanoVG 进程按 ABBA 顺序运行，结果为：

| 场景 | 原始 | 第 4 轮 | 第 5 轮 | 配对 NanoVG GL3 | 第 5 轮结果 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 4.682 ms | 2.690 ms | 3.947 ms | WhatsCanvas 快 31.8% |

---

以上内容是对原文的中文翻译，保留了原始结构、术语和代码示例。若需要，我也可以继续把这份文档进一步整理成更适合文档站点发布的版式，例如补充目录、标题层级调整，或同步生成中文版索引入口。
