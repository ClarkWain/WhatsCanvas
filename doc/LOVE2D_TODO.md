# LÖVE2D 可借鉴优化项 — 实施 TODO

> 按 LOVE2D_Borrowable_Checklist.md 原始顺序 (#1-#20) 实施
> 每项完成后：子代理代码审查 → 修复问题 → 构建验证 → 更新状态 → git 提交

| # | 改进项 | 分数 | 耗时 | 状态 |
|---|--------|------|------|------|
| 1 | Canvas/Texture 统一 | 5 | XL | ✅ 已完成 |
| 2 | 流式批处理 (Batched Draw) | 5 | L | ✅ 已完成 |
| 3 | 多后端渲染抽象 | 4 | XL | ✅ 已完成 |
| 4 | 统一 DrawCommand 结构 | 5 | M | 🔧 进行中 |
| 5 | GraphicsState 栈扩展 | 4 | S | ✅ 已完成 |
| 6 | Shader validateDrawState | 4 | S | 🔧 进行中 |
| 7 | StreamBuffer 顶点流 | 5 | M | ⬜ 待开始 |
| 8 | Lazy Render Pass | 3 | S | ⬜ 待开始 |
| 9 | Gamma 校正管线 | 3 | M | ⬜ 待开始 |
| 10 | 帧边界 present/reset | 4 | S | ⬜ 待开始 |
| 11 | Volatile 资源生命周期 | 4 | M | ⬜ 待开始 |
| 12 | 临时资源池 | 3 | M | ⬜ 待开始 |
| 13 | 异步 Readback + Fence Sync | 3 | M | ⬜ 待开始 |
| 14 | 全局 Quad/Fan Index Buffer | 4 | S | ⬜ 待开始 |
| 15 | Texel Buffer 抽象 | 2 | M | ⬜ 待开始 |
| 16 | DrawMode/ArcMode 组合枚举 | 3 | S | ✅ 已完成 |
| 17 | Deprecation Warning 系统 | 2 | S | ⬜ 待开始 |
| 18 | isPixelFormatSupported 能力查询 | 4 | S | ⬜ 待开始 |
| 19 | SpriteBatch 批量绘制 | 3 | L | ⬜ 待开始 |
| 20 | 窗口 resize 无痛重建 | 4 | M | ⬜ 待开始 |
