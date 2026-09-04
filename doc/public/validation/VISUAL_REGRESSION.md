# Visual Regression and Baseline Policy

This document is the single source of truth for visual-regression signals,
baseline storage, update rules, and the minimum validation expected for each
kind of change. WhatsCanvas uses two complementary image signals:

- exact pixel hashes for deterministic local gates
- fuzzy PPM comparison for driver-sensitive scenes

## Exact Hash Gate

Exact hashes are fast and strict. They are best for local fixed-time, non-MSAA runs where the GPU and driver are stable.

Common environment:

```sh
WHATSCANVAS_EXIT_AFTER_FIRST_FRAME=1
WHATSCANVAS_FIXED_TIME_SECONDS=1.25
WHATSCANVAS_DISABLE_MSAA=1
WHATSCANVAS_PRINT_PIXEL_HASH=1
```

## Fuzzy PPM Comparison

Use `scripts/compare_ppm_fuzzy.py` when exact hashes are too sensitive but the rendered image should still remain visually close to a baseline.

```sh
python scripts/compare_ppm_fuzzy.py baseline.ppm candidate.ppm \
  --max-channel-delta 3 \
  --max-mean-delta 0.75 \
  --max-changed-percent 5.0
```

The script prints machine-readable metrics:

- `FUZZY_PPM_COMPARE_MAX_CHANNEL_DELTA`
- `FUZZY_PPM_COMPARE_MEAN_DELTA`
- `FUZZY_PPM_COMPARE_CHANGED_PERCENT`
- `FUZZY_PPM_COMPARE_RESULT`

The script currently supports binary `P6` PPM files with `maxval=255`, matching `Canvas::savePixelsPPM`.

## Baseline Categories

| Category | Primary Gate | Baseline Type | Notes |
| --- | --- | --- | --- |
| Core API behavior | `ctest -L unit` | Assertions | Geometry, state, text utilities, resource lifecycle, and backend contracts. |
| Public API reference | `scripts/api_reference_check.*` | Generated-doc freshness | Fails when `doc/public/reference/API_REFERENCE.md` is stale relative to `include/wsc/`. |
| Version consistency | `scripts/version_consistency_check.*` | Metadata consistency | Keeps project versions, public macros, docs, and package naming aligned. |
| Package consumer | `scripts/package_consumer_smoke.*` | External CMake build | Verifies that a separate project can consume the installed package. |
| Deterministic smoke | `scripts/smoke_test.*` | Exact hash / first-frame checks | Use fixed time and disabled MSAA where possible. |
| Text rendering | `scripts/text_pixel_regression.*` | Fuzzy PPM | Covers `font-regression` and `text-showcase` by default. |
| Effects and scenes | `scripts/validation_scene_smoke.*` plus captures | Exact hash or fuzzy PPM | Covers gradients, shadows, blend modes, strokes, and other driver-sensitive effects. |
| Cross-backend filters | `*FilterPixelParityTests` | In-memory fuzzy premultiplied RGBA | Records error metrics, worst channel/location, and hashes against Software. |
| OpenGLES build health | `scripts/opengles_build_smoke.*` | Build plus Mesa EGL parity | Protects GL-family portability and GLES filter execution. |

## Official Baseline Storage

- Keep committed baselines under `tests/baselines/`.
- Never store authoritative baselines in generated build directories.
- Keep text baselines under `tests/baselines/text/`.
- Give review captures stable names containing the scene and, when needed, the
  platform and configuration.

## Updating Text Baselines

Windows:

```bat
set WHATSCANVAS_UPDATE_TEXT_BASELINES=1
cmd /c scripts\text_pixel_regression.bat
```

macOS / Linux:

```sh
WHATSCANVAS_UPDATE_TEXT_BASELINES=1 scripts/text_pixel_regression.sh
```

Accept a baseline update only when the rendering change is intentional, the
affected scenes are named in the commit or pull request, thresholds were not
loosened to hide unrelated changes, and the update is reproducible locally.

## Capturing A Validation Scene

```sh
WHATSCANVAS_VALIDATION_SCENE=gradient-effect \
WHATSCANVAS_EXIT_AFTER_FIRST_FRAME=1 \
WHATSCANVAS_FIXED_TIME_SECONDS=1.25 \
WHATSCANVAS_DISABLE_MSAA=1 \
WHATSCANVAS_CAPTURE_PPM=build/gradient-effect.ppm \
./build/WhatsCanvasDemo
```

On Windows PowerShell:

```powershell
$env:WHATSCANVAS_VALIDATION_SCENE = "gradient-effect"
$env:WHATSCANVAS_EXIT_AFTER_FIRST_FRAME = "1"
$env:WHATSCANVAS_FIXED_TIME_SECONDS = "1.25"
$env:WHATSCANVAS_DISABLE_MSAA = "1"
$env:WHATSCANVAS_CAPTURE_PPM = "build\\gradient-effect.ppm"
build\\Debug\\WhatsCanvasDemo.exe
```

## Threshold Rules

- Use exact hashes for stable non-MSAA smoke gates.
- Use fuzzy comparison for shadows, gradients, text, and other driver-sensitive scenes.
- Tighten thresholds when a scene becomes more deterministic.
- Loosen thresholds only with an explanation and image review.
- Prefer a targeted scene over broadly relaxing an existing one.
- Cover pure state and geometry behavior with unit tests where possible.
- Review fuzzy threshold changes like code changes because loose thresholds can hide real regressions.

## Required Checks by Change Type

| Change Type | Minimum Checks |
| --- | --- |
| Public API or packaging | `ctest -L unit`; package configure/build if touched |
| Install/package CMake | `scripts/package_consumer_smoke.*` |
| Version or release workflow | `scripts/version_consistency_check.*` |
| Public headers | Regenerate `doc/public/reference/API_REFERENCE.md`, then run `scripts/api_reference_check.*` |
| Text/font stack | `ctest -L text`, `scripts/text_pixel_regression.*` |
| GL shader/render path | Smoke test, relevant validation scene, fuzzy comparison when captured |
| Image-filter backend | Matching `*FilterPixelParityTests`; benchmark smoke for performance wiring |
| OpenGLES portability | `scripts/opengles_build_smoke.*` and GLES parity under Mesa EGL |
| Documentation only | Link/keyword review and no private project context |

Detailed execution records and platform matrices live under `internal/validation/`;
they are project evidence, not normative user documentation.
