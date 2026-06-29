# WhatsCanvas GL-Family Blend Mode Audit

This audit records how public `Paint::BlendMode` values map to the current GL-family backend.

## Current Pipeline Assumption

- Shader outputs are treated as non-premultiplied RGBA.
- `RenderContext::applyBlendMode` enables `GL_BLEND` and uses `glBlendEquation(GL_FUNC_ADD)`.
- Blend state is applied per command before draw execution.
- Points, lines, paths, images, text, and saveLayer restore commands all snapshot a `DrawBlendMode`.
- `SpriteBatch` batches compatible image commands with the command blend mode preserved.
- There is no public premultiplied-alpha output option today.

## Public To Renderer Mapping

| Public paint mode | Renderer mode | GL RGB factors | GL alpha factors | Status |
| --- | --- | --- | --- | --- |
| `SRC_OVER` | `SrcOver` | `SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA` | `ONE`, `ONE_MINUS_SRC_ALPHA` | Standard default |
| `SRC` | `Src` | `ONE`, `ZERO` | `ONE`, `ZERO` | Replaces destination |
| `DST` | `Dst` | `ZERO`, `ONE` | `ZERO`, `ONE` | Keeps destination |
| `CLEAR` | `Clear` | `ZERO`, `ZERO` | `ZERO`, `ZERO` | Clears affected pixels |
| `SRC_IN` | `SrcIn` | `DST_ALPHA`, `ZERO` | `DST_ALPHA`, `ZERO` | Porter-Duff approximation |
| `DST_IN` | `DstIn` | `ZERO`, `SRC_ALPHA` | `ZERO`, `SRC_ALPHA` | Porter-Duff approximation |
| `SRC_OUT` | `SrcOut` | `ONE_MINUS_DST_ALPHA`, `ZERO` | `ONE_MINUS_DST_ALPHA`, `ZERO` | Porter-Duff approximation |
| `DST_OUT` | `DstOut` | `ZERO`, `ONE_MINUS_SRC_ALPHA` | `ZERO`, `ONE_MINUS_SRC_ALPHA` | Porter-Duff approximation |
| `SRC_ATOP` | `SrcAtop` | `DST_ALPHA`, `ONE_MINUS_SRC_ALPHA` | `DST_ALPHA`, `ONE_MINUS_SRC_ALPHA` | Porter-Duff approximation |
| `DST_ATOP` | `DstAtop` | `ONE_MINUS_DST_ALPHA`, `SRC_ALPHA` | `ONE_MINUS_DST_ALPHA`, `SRC_ALPHA` | Porter-Duff approximation |
| `XOR` | `Xor` | `ONE_MINUS_DST_ALPHA`, `ONE_MINUS_SRC_ALPHA` | `ONE_MINUS_DST_ALPHA`, `ONE_MINUS_SRC_ALPHA` | Porter-Duff approximation |
| `ADD` | `Add` | `SRC_ALPHA`, `ONE` | `ONE`, `ONE` | Additive |
| `MULTIPLY` | `Multiply` | `DST_COLOR`, `ZERO` | `DST_ALPHA`, `ZERO` | Fixed-function approximation |
| `SCREEN` | `Screen` | `ONE`, `ONE_MINUS_SRC_COLOR` | `ONE`, `ONE_MINUS_SRC_ALPHA` | Fixed-function approximation |

## Important Limitations

- These modes rely on fixed-function blending. They are not a full shader-composited blend stack.
- Multiply and screen are approximate for the current non-premultiplied outputs.
- Advanced Photoshop-style modes are not implemented.
- A premultiplied-alpha option would need a separate public contract and shader/output audit.
- Exact visual results can vary with framebuffer format and driver behavior, so blend-heavy scenes are good candidates for fuzzy visual comparison.

## Validation Coverage

- `PaintStateTests` verifies public blend-mode state round trips.
- The showcase demo exercises `ADD`, `MULTIPLY`, `SCREEN`, `SRC_IN`, and `DST_OUT`.
- The validation scene suite includes blend usage in gradient/effect and saveLayer scenes.
- `scripts/compare_ppm_fuzzy.py` can be used for blend-heavy visual baselines where exact hashes are too strict.
