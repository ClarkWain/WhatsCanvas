# 渲染优化可借鉴项 — 实施 TODO

> 按原始优化检查表顺序 (#1-#20) 实施
> 2026-06-20 已按 20 个对应提交逐项审查；`🛠` 表示本轮发现并修复了问题。

| # | 改进项 | 分数 | 耗时 | 状态 |
|---|--------|------|------|------|
| 1 | Canvas/Texture 统一 | 5 | XL | 🛠 已审查并修复：有效性语义、render-target UV 方向 |
| 2 | 流式批处理 (Batched Draw) | 5 | L | 🛠 已审查并修复：补齐批后 draw 统计 |
| 3 | 多后端渲染抽象 | 4 | XL | 🛠 已审查并修复：仅自动选择已实现的 OpenGL 后端 |
| 4 | 统一 DrawCommand 结构 | 5 | M | 🛠 已审查并修复：所有具体 Command 补齐只读 `data()` |
| 5 | GraphicsState 栈扩展 | 4 | S | 🟠 已审查：状态字段已加入，Canvas 全局状态 API 待接入 |
| 6 | Shader validateDrawState | 4 | S | ✅ 已审查通过：5 个绘制程序已接入 |
| 7 | StreamBuffer 顶点流 | 5 | M | 🛠 已审查并修复：Path/Points/Lines/Image/Text 绘制程序统一使用 StreamBuffer |
| 8 | Lazy Render Pass | 3 | S | ✅ 已审查通过：offscreen FBO 延迟到首次绘制激活 |
| 9 | Gamma 校正管线 | 3 | M | 🛠 已审查并修复：Path/Points/Lines/Text/Image tint 统一进入线性颜色 |
| 10 | 帧边界 present/reset | 4 | S | 🛠 已审查并修复：统计覆盖所有 Command；精确 GPU draw 计数待完善 |
| 11 | Volatile 资源生命周期 | 4 | M | 🛠 已审查并修复：GLProgram move 保留 shader 源码；其余 GPU 资源待覆盖 |
| 12 | 临时资源池 | 3 | M | 🛠 已审查并修复尺寸复用；尚未接入 offscreen 主路径 |
| 13 | 异步 Readback + Fence Sync | 3 | M | 🛠 已审查并修复 map 失败路径；尚未接入 Canvas 公共 API |
| 14 | 全局 Quad/Fan Index Buffer | 4 | S | 🛠 已审查并接入后端生命周期；draw program 尚未使用 |
| 15 | Texel Buffer 抽象 | 2 | M | 🟡 已审查：组件可用，尚无渲染路径使用 |
| 16 | DrawMode/ArcMode 组合枚举 | 3 | S | ✅ 已审查通过：OPEN/CHORD/PIE 已实现 |
| 17 | Deprecation Warning 系统 | 2 | S | 🟡 已审查：Tracker 可用，尚未标记具体弃用 API |
| 18 | isPixelFormatSupported 能力查询 | 4 | S | 🛠 已审查并修复格式探测参数，并接入后端初始化 |
| 19 | SpriteBatch 批量绘制 | 3 | L | 🛠 已审查并修复 shader/固定 GL 状态；尚未接入 Renderer/公共 API |
| 20 | 窗口 resize 无痛重建 | 4 | M | 🟠 已审查：ResizeHandler 已接入通知，资源重建监听待覆盖 |

状态含义：`✅` 表示主路径已使用且审查通过；`🛠` 表示本轮已修复；`🟠` 表示部分主路径已接入；`🟡` 表示基础组件存在但尚未产生默认性能收益。
