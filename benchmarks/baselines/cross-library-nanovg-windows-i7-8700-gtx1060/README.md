# WhatsCanvas / NanoVG Cross-Library Baseline

This baseline compares equivalent output, not unrelated demos. Both adapters
rendered the three contract scenes at 1920 x 1080 in Release, with 30 timed
frames after 5 warmup frames. Every frame timing includes `glFinish`; readback
and hashing are excluded.

## Environment

- CPU: Intel Core i7-8700, 12 hardware threads
- GPU: NVIDIA GeForce GTX 1060 3GB
- Driver: NVIDIA 560.94, OpenGL 3.3
- OS: Windows 10 x86_64
- WhatsCanvas: `0532fbd99e65567c9d3883a6678b9b3399859031`
- NanoVG: `ce3bf745eb2d2dbc14a50bf2446783f691ac4353`
- Contract: `1.1.0`

## Three-process result

Each cell lists the complete-frame median from an independent process. The
aggregate is the median of those three process medians, which prevents the
noisy WhatsCanvas geometry run from becoming the headline result.

| Scene | Library | Run 1 | Run 2 | Run 3 | Aggregate |
| --- | --- | ---: | ---: | ---: | ---: |
| `geometry_stress` | WhatsCanvas | 24.620 ms | 25.659 ms | 38.388 ms | 25.659 ms |
| `geometry_stress` | NanoVG | 4.156 ms | 5.217 ms | 4.316 ms | 4.316 ms |
| `image_grid` | WhatsCanvas | 0.310 ms | 0.308 ms | 0.299 ms | 0.308 ms |
| `image_grid` | NanoVG | 0.286 ms | 0.372 ms | 0.420 ms | 0.372 ms |
| `contract_text_latin` | WhatsCanvas | 14.329 ms | 17.323 ms | 15.911 ms | 15.911 ms |
| `contract_text_latin` | NanoVG | 3.303 ms | 3.334 ms | 3.851 ms | 3.334 ms |

On this machine and contract, NanoVG is 5.95x faster in dynamic mixed geometry
and 4.77x faster in the fixed Latin text scene. WhatsCanvas is 1.21x faster in
the reused image-grid scene. These ratios are scene-specific and are not a
general ranking of either library.

## Quality gates

| Scene | NanoVG MAE | RMSE | Pixels over channel threshold | Result |
| --- | ---: | ---: | ---: | --- |
| `geometry_stress` | 0.242 | 1.003 | 0.000% | PASS |
| `image_grid` | 0.275 | 0.600 | 0.019% | PASS |
| `contract_text_latin` | 7.744 | 26.566 | 8.551% | PASS |

The text adapter uses the same vendored Roboto file and 576 strings. NanoVG's
font-size unit is converted by a fixed `0.875` factor and top alignment is
offset to match the rendered WhatsCanvas pixel-height contract. This aligns
visible size and baseline; it does not add shaping, substitute glyphs, reduce
draw count, or reuse a rendered text bitmap. Remaining differences come from
FreeType/HarfBuzz versus stb/fontstash rasterization, kerning, and shaping.

The contract text threshold still rejects a background-only capture: measured
blank-output error was MAE 9.917, RMSE 32.413, and 10.246% changed pixels.

All 18 raw JSONL records are stored beside this report. PPM captures are
intentionally omitted because they are deterministic 6 MiB artifacts generated
by the documented command.
