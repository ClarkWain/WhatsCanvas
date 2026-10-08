# User-visible performance audit — September 2026

Measured on the local Windows Release build (Intel i7-8700 class CPU, desktop
OpenGL) on `fix/render-parity-software-blur`. These are deterministic stress
scenes, not production traces or mobile-device results. Timings are median
frame times from 30 measured frames after 5 warm-up frames unless noted.
The 60 FPS frame budget is 16.7 ms.

## Findings

| Priority | User situation | Reproduction and evidence | Cause and next action |
| --- | --- | --- | --- |
| P1 | Text appears for the first time after opening a screen | `text_churn`, 960 × 540 OpenGL: first frame 427–439 ms in repeated fresh processes, with 424–436 ms in recording; steady frame about 6 ms. A temporary local probe measured about 401 ms in first `createBasicTextBackend()` | `Canvas::drawText()` creates the backend lazily. Its constructor calls `registerSystemFontFallbacks()`, which enumerates installed fonts through DirectWrite. Reduce the default-alias lookup to the families needed for fallback while preserving the explicit full discovery/refresh API. Measure first text separately from steady text and verify CJK, emoji, user fonts, and refreshed installs. Simply prewarming would move the pause rather than remove it. |
| P1 | A page has many blurred card shadows at 1080p | `shadow_grid` (36 shadows): OpenGL 19.54 ms/frame, p95 20.68 ms; GPU timing 18.41 ms. The same scene is 5.82 ms at 960 × 540 | `DrawShadowCommand::execute()` allocates full-canvas blur targets and runs two full-canvas passes for each shadow. Bound the shadow work to its transformed silhouette plus Gaussian support and composite only that region. Check clipped and transformed shadows, glyph shadows, RGBA parity, and GPU timing at both resolutions. |
| P1 for Software fallback | Layer-heavy UI or scrolling panels at 1080p | `clip_layers` (16 clipped panels with animated stripes): Software 649 ms/frame, p95 672 ms. About 397 ms is recording, including about 385 ms of layer rendering; final submit costs about 251 ms | Every changed panel is rasterized into a layer and then composited in Software. Profile rectangle/clip raster loops and target allocation before a focused optimization. A caller can retain static panels as rasterized Pictures and draw changing content separately, but this animated benchmark cannot be wholly cached. |
| P2 for Software fallback | Ordinary 1080p UI or image gallery | Software `rounded_ui` 112 ms/frame and `image_grid` 165 ms/frame; at 960 × 540 these are 29 and 36 ms. OpenGL at 1080p is 0.86 and 0.29 ms respectively | Pixel count dominates Software raster cost. Keep the Software backend as a functional fallback for large, continuously redrawn surfaces; test reduced resolution, retained raster content, and direct raster hot spots before promising interactive rates. |
| P2 | Synchronous screenshot/readback during an animation | 1080p OpenGL `readPixelsRGBA()` took 4.9–6.8 ms for an 8 MiB frame, outside the measured render interval | The synchronous API can stall the GPU and consume a substantial part of a frame. Use the existing asynchronous readback path where the caller can accept a later result; test end-to-end capture latency separately. |

OpenGL's changing image workload was not a bottleneck in this run: 1,000 draws
using 64 textures, 50% rounded images and 50% state changes took 3.15 ms/frame
(p95 3.84 ms) at 1080p. Do not prioritize image batching on this evidence.
An additional temporary full-frame RGBA update probe (8 MiB uploaded and
drawn every frame at 1080p) measured 2.89 ms/frame (p95 4.39 ms) on OpenGL,
including 2.52 ms in recording/upload. Software took 53.29 ms/frame (p95
53.91 ms), mostly final raster work. The probe scene was removed after
measurement so the published benchmark scene set stays unchanged. These
numbers do not include camera decoding, pixel conversion, or video pacing.

An early DirectWrite experiment changed the collection refresh flag while the
default path still enumerated every installed family. First-frame recording
remained about 351–358 ms, so it was reverted. After restricting the default
path to its candidate families, a focused probe measured 92–97 ms in
`GetSystemFontCollection(TRUE)` and less than 1 ms enumerating the selected
families. The first default lookup now uses the cached collection; explicit
discovery and refresh still request a fresh collection.

## Implemented optimizations and acceptance

| Workload | Before | After | Validation |
| --- | ---: | ---: | --- |
| First `text_churn` frame, 960 × 540 | 427–439 ms in fresh processes | 40.08, 40.89, 42.23 ms in three final fresh processes | Default aliases discover only candidate Windows families from DirectWrite's cached collection; full installed-font discovery and explicit refresh still force a current collection. The frame hash stayed `8e0605b49abc5ea9`. Font manager and discovery tests pass, including a cold-versus-full alias comparison. |
| 36 OpenGL shadows, 1920 × 1080 | 19.54 ms median, 18.41 ms GPU | 3.45 ms median, 3.09 ms GPU | Blur and composite are bounded to the shadow silhouette plus Gaussian support. Captured PPM is byte-identical before and after; the frame hash stayed `72c1c4e0ba10105f`. |
| Software frosted glass, 960 × 540 | 327.20 ms median | 217.34 ms in an earlier run; 241.28 and 241.31 ms in two final isolated runs | Downsample opaque Clamp blur, reuse safe backdrops, and composite larger images by independent rows. The frame hash stayed `8eb6d81e75edd42d`; the initial blur optimization's pre/post RGB capture had maximum delta 2 and mean delta 0.049. Even the slower final repeat is 26% below baseline. |
| 16 Software clipped layers, 1920 × 1080 | 649.80 ms median | 82.12–85.02 ms median across final runs | Reuse clip coverage within and across frames, rasterize it only within the existing scissor, avoid the redundant first-mask multiplication, and process independent rows of large axis-aligned images in parallel. Recording fell from 402.34 to about 25 ms median; final submit fell from about 249 to 57 ms. Captured PPM is byte-identical and the frame hash stayed `038d261d2869a07e`. |

The 56-scene Software/OpenGL parity probe passes with no render or delta
failures. The global RGBA maximum is 2 only for the new rotated-shadow scene;
the other 55 scenes stay at 1 or less. A control render with bounded shadow
blur disabled also has a maximum of 2 for the rotated scene, so that
difference predates the optimization. Sequential and clipped shadows are
included. The focused rendering and font tests pass.

After the first performance pass, three further fresh-process first frames
were 134.7, 137.6, and 130.8 ms with the same text hash. A further 30-frame
OpenGL shadow run had a 3.36 ms median and the same frame hash. The subsequent
Software clip-region optimization was repeated in three separate 30-frame
runs with medians of 228.15, 227.89, and 227.65 ms. Adding a 16 MiB bounded
cross-frame coverage cache reduced the next three medians to 143.73, 144.47,
and 143.47 ms. Parallelizing independent image rows reduced three further
medians to 82.31, 82.47, and 82.12 ms. The captured PPM remained byte-identical
to the earlier stages. On this scene, steady RSS rose from about 22.6 to
31.9 MiB because of the cache, whose retained coverage is capped at 12 MiB.
A temporary probe before row parallelization changed all 16 rounded clips each
frame and measured 275 ms with the cache versus 290 ms without it; the probe
was removed after measurement. A focused test compares changing clips and a
resized canvas against fresh canvases.

After changing only the first default font lookup to use DirectWrite's cached
collection, three fresh-process first frames were 46.15, 43.95, and 42.73 ms.
With temporary profiling removed, the final source measured 40.08, 40.89, and
42.23 ms. The first-frame text hash remained `8e0605b49abc5ea9`. The explicit
font-refresh path and cold-versus-full alias comparison passed the Windows
font discovery tests. A temporary probe measured about 1.7 ms to construct the
text backend and register default aliases after the collection change; the
remaining cold-frame cost includes rasterizing 120 text draws in this stress
scene. These results apply to the local installed-font set.

The Software layer scene still spends about 57 ms in final frame rasterization
and is far outside a 16.7 ms frame budget. Large continuously redrawn Software
surfaces, dynamic full-frame images, and synchronous readback remain separate
performance work. No mobile GPU or release-device timings have been measured.
An isolated final `clip_layers` repeat measured 85.02 ms median with the same
frame hash; its p95 reached 114.94 ms, so frame-time variation remains.

## Reproduction

```powershell
.\build\Release\WhatsCanvasPerformanceSuite.exe --backend opengl --scene text_churn --frames 30 --warmup 5 --width 960 --height 540 --output out\text-cold.jsonl
.\build\Release\WhatsCanvasPerformanceSuite.exe --backend opengl --scene shadow_grid --frames 30 --warmup 5 --width 1920 --height 1080 --gpu-timing --output out\shadow-1080p.jsonl
.\build\Release\WhatsCanvasPerformanceSuite.exe --backend software --scene rounded_ui,image_grid,clip_layers --frames 30 --warmup 5 --width 1920 --height 1080 --output out\software-1080p.jsonl
```

The suite now emits `cold_record_ms`, `cold_end_frame_cpu_ms`, and
`cold_gpu_wait_ms` next to `cold_total_ms` to distinguish an initial text
setup pause from drawing or GPU work. For a release decision, run each case in
at least three fresh processes, compare process medians, retain pixel hashes,
and verify memory use. The local results above establish improvements for the
listed scenes only; they do not establish cross-device performance.
The shadow command is shared with OpenGL ES builds, so mobile devices may
face the same work scaling; that is an inference from code, not a device
measurement.
