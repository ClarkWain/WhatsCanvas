# Render parity and performance validation — September 2026

Scope: the Windows Release build with Software and desktop OpenGL enabled, based
on `1.2.1` (`03cc4e8`). This is a development gate for the rendering changes;
it is not a mobile hardware sign-off.

## Defects and changes

| Finding | Cause | Fix |
| --- | --- | --- |
| A non-AA rectangle at half-pixel coordinates differed along its top and bottom edges | The software triangle and axis-aligned image paths assigned horizontal boundary pixels to the opposite edge from OpenGL | Match OpenGL's top-left rasterization rule in canvas coordinates |
| Shadows over an opaque background had up to 64 levels of alpha difference | The OpenGL shadow composite used the color blend factors for alpha, multiplying source alpha twice | Use separate SrcOver alpha factors |
| The Software 960 × 540 frosted-glass scene took about 327 ms per frame | Large software blur always ran at full resolution; software command inspection also disabled backdrop reuse | Use half-resolution blur only for opaque Clamp inputs, and enable command fingerprinting with mutable software images excluded from reuse |

The software cache preserves a valid earlier layer across a later uncacheable
layer. OpenGL retains its existing invalidation behavior because its render
target pool can reuse a texture behind a cached resource. Transparent blur
inputs keep their full-resolution path so edge alpha is not changed by the
optimization.

## Acceptance gates

1. Build Release targets for the parity probe, filter benchmark, and relevant
   rendering tests. Run the probe at its default 256 × 256 size with
   `--require-gl --max-delta 2`. All 56 scenes must render. The original 54
   scenes and `shadow_sequence` must stay at maximum RGBA difference 1;
   `shadow_transformed` may reach 2. This includes the non-AA half-pixel
   rectangle.
2. Pass the Software renderer and golden tests, OpenGL backdrop and filter
   parity tests, render statistics, matrix/clip, image resource lifecycle,
   context lifecycle, and Software library tests. The tests cover opaque
   backdrop downsampling and reuse, changed geometry invalidation, mutable
   image invalidation, transparent blur fallback, and shadow alpha over an
   opaque background.
3. In `overlapping_frosted_glass` at 960 × 540, 5 warm-up frames and 30
   measured frames, repeat the Software benchmark in three separate processes.
   The median of those medians must improve by at least 10% from the same
   machine's pre-change baseline. The `inner_shadow_grid` workload must not
   regress by more than 10%. Repeat the OpenGL benchmark to catch a large
   unintended regression. Compare the pre-change and optimized Software
   frosted-glass PPM captures with max RGB channel difference at most 2 and
   mean RGB difference at most 0.05. Check the frame hash remains stable
   across repeated optimized runs.
4. Run `git diff --check`. Before release, rerun relevant Android OpenGL ES
   and iOS/Metal integration checks if that release includes these changes.

## Reproduction

```powershell
cmake --build build --config Release --target WhatsCanvasParityProbe WhatsCanvasImageFilterBenchmarks
.\build\Release\WhatsCanvasParityProbe.exe --out out\parity-final --require-gl --max-delta 2
ctest --test-dir build -C Release --output-on-failure -R 'WhatsCanvas(ContextLifecycle|ImageResourceLifecycle|MatrixClip|SoftwareLib|RenderStats|SoftwareGolden|OpenGLBackdropFilter|OpenGLFilterPixelParity|SoftwareRenderer)Tests'
.\build\Release\WhatsCanvasImageFilterBenchmarks.exe --backend software --frames 30 --warmup 5 --width 960 --height 540
.\build\Release\WhatsCanvasImageFilterBenchmarks.exe --backend opengl --frames 30 --warmup 5 --width 960 --height 540
git diff --check
```

## Initial local result (before the two added shadow scenes)

| Gate | Result |
| --- | --- |
| RGBA parity | 54 rendered, 0 render failures, 0 delta failures; global max channel delta 1 |
| Focused CTest suite | 9/9 passed |
| Software frosted glass | Baseline 327.198 ms (one process); optimized medians 267.420, 266.991, 267.503 ms (three processes). Median improvement 18.3%. Optimized hash `8eb6d81e75edd42d` in all three runs |
| Software inner shadow | Baseline 57.485 ms; optimized medians 56.989, 57.026, 56.950 ms |
| OpenGL frosted glass | Baseline 3.437 ms; latest 2.926 ms. Timing variance is larger at this scale, so this is a regression check rather than an attributed speedup |
| Software frosted-glass RGB capture | Max delta 2, mean delta 0.049026 from the pre-change capture |

The Software frosted-glass scene still takes roughly 267 ms per frame on this
machine. It is unsuitable for interactive frame rates at this size. The
relative performance gate passed, but further work would require reducing
backdrop replay and final Software raster cost. The current pixel and speed
claims cover this Windows desktop configuration and the listed scenes only.

## Additional user-visible performance gate

The September performance pass adds `shadow_sequence` and
`shadow_transformed` to the parity probe. Run all 56 scenes with
`--max-delta 2`: 0 render failures and 0 delta failures are required. Only
`shadow_transformed` may reach 2; the previous 54 scenes and the new sequence
must remain at 1 or less. The rotated scene also reaches 2 when the bounded
shadow optimization is disabled.

For the Windows Release build, compare a captured frame and its hash before
and after each optimization. The 1920 × 1080 `shadow_grid` and `clip_layers`
PPM files must be byte-identical. Median frame time should remain below 5 ms
for OpenGL `shadow_grid` and below 100 ms for Software `clip_layers` on the
same machine and settings. Run the cold `text_churn` case in three fresh
processes at 960 × 540; require each first frame below 60 ms and an unchanged
frame hash. These are local regression thresholds, not device promises.

Current results: 56/56 parity scenes passed; OpenGL shadow median 3.45 ms;
Software clipped-layer median 320.01 ms after the first optimization;
first-text frames 152.1, 128.7, and 135.2 ms. A later run measured 3.36 ms
for shadows and first-text frames of 134.7, 137.6, and 130.8 ms. The final
Software clip-region change measured 228.15, 227.89, and 227.65 ms in three
separate 30-frame runs. The bounded cross-frame cache then measured 143.73,
144.47, and 143.47 ms. Parallel image rows then measured 82.31, 82.47, and
82.12 ms; reducing the cache cap from 16 to 12 MiB retained an 82.56 ms median.
The later Windows default-font lookup change measured final-source first frames
of 40.08, 40.89, and 42.23 ms in three fresh processes, with the same text hash. Full
installed-font discovery and explicit refresh still force a fresh collection.
The Software frosted-glass median was 217.34 ms in one later run and 241.28 and
241.31 ms in two final isolated repeats. Its pixel hash stayed unchanged; the
earlier before/after PPM pairs matched byte for byte. Font
manager, system discovery, text backend contract, Software
renderer/golden, OpenGL backdrop/filter parity, matrix/clip, image resource,
and context lifecycle tests passed.
