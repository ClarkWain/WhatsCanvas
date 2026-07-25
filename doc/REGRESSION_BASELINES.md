# Regression Baseline Policy

WhatsCanvas uses automated tests, smoke scenes, exact pixel hashes, and fuzzy PPM comparison to keep a lightweight renderer stable without pretending every GPU driver emits identical pixels.

## Baseline Categories

| Category | Primary Gate | Baseline Type | Notes |
| --- | --- | --- | --- |
| Core API behavior | `ctest -L unit` | Assertions | Geometry, state, text utilities, resource lifecycle, and backend contracts. |
| Public API reference | `scripts/api_reference_check.*` | Generated doc freshness | Fails when `doc/API_REFERENCE.md` is stale relative to `include/wsc/`. |
| Version consistency | `scripts/version_consistency_check.*` | Metadata consistency | Keeps CMake project version, public version macros, docs, and package naming in sync. |
| Package consumer | `scripts/package_consumer_smoke.*` | External CMake build | Verifies the install/package config can be consumed by a separate project. |
| Deterministic smoke | `scripts/smoke_test.*` | Exact hash / first-frame checks | Use fixed time and disabled MSAA where possible. |
| Text rendering | `scripts/text_pixel_regression.*` | Fuzzy PPM | Covers `font-regression` and `text-showcase` by default. |
| Effects and scenes | `scripts/validation_scene_smoke.*` plus manual captures | Exact hash or fuzzy PPM depending on scene | Gradients, shadows, blend modes, strokes, dashes, and other driver-sensitive effects. |
| Cross-backend filters | `*FilterPixelParityTests` | In-memory fuzzy premultiplied RGBA | One deterministic scene is compared with Software; structured output records max/mean error, bad-pixel ratio, worst channel/location, and hashes. |
| OpenGLES build health | `scripts/opengles_build_smoke.*` | Configure/build plus Mesa EGL parity | Ensures GL-family portability and GLES filter shader execution do not regress. |

## Official Baseline Storage

- Keep committed baselines under `tests/baselines/`.
- Do not store baselines in generated build folders.
- Text baselines live under `tests/baselines/text/`.
- Scene captures intended for review should use stable names that include the scene and platform/config when needed.

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

Review baseline image changes like source changes. A baseline update is acceptable when:

- the rendering change is intentional
- the changed scenes are named in the commit or PR description
- fuzzy thresholds were not loosened to hide unrelated changes
- the same update path can be reproduced locally

## Capturing Validation Scenes

Common environment:

```sh
WHATSCANVAS_VALIDATION_SCENE=gradient-effect
WHATSCANVAS_EXIT_AFTER_FIRST_FRAME=1
WHATSCANVAS_FIXED_TIME_SECONDS=1.25
WHATSCANVAS_DISABLE_MSAA=1
WHATSCANVAS_CAPTURE_PPM=build/gradient-effect.ppm
```

Use exact hashes for stable local checks:

```sh
WHATSCANVAS_PRINT_PIXEL_HASH=1 ./build/WhatsCanvasDemo
```

Use fuzzy PPM comparison when drivers or shader precision can create tiny differences:

```sh
python scripts/compare_ppm_fuzzy.py baseline.ppm candidate.ppm \
  --max-channel-delta 3 \
  --max-mean-delta 0.75 \
  --max-changed-percent 5
```

## Threshold Rules

- Tighten thresholds when the scene becomes more deterministic.
- Loosen thresholds only with an explanation and an image review.
- Prefer adding a targeted scene over broadly relaxing an existing one.
- Text, shadows, gradients, and antialiasing usually need fuzzy comparison.
- Pure state/geometry behavior should usually be covered by unit tests instead of image thresholds.

## Required Checks by Change Type

| Change Type | Minimum Checks |
| --- | --- |
| Public API or packaging | `ctest -L unit`, package configure/build if touched |
| Install/package CMake changes | `scripts/package_consumer_smoke.*` |
| Version or release workflow changes | `scripts/version_consistency_check.*` |
| Public header changes | `scripts/api_reference_check.*` after regenerating `doc/API_REFERENCE.md` |
| Text/font stack | `ctest -L text`, `scripts/text_pixel_regression.*` |
| GL shader/render path | smoke test, relevant validation scene, fuzzy comparison if captured |
| Image-filter backend | matching `*FilterPixelParityTests`; benchmark smoke if performance wiring changed |
| OpenGLES portability | `scripts/opengles_build_smoke.*` and GLES parity under Mesa EGL |
| Docs only | link/keyword review and no private project context |

See also `doc/VISUAL_REGRESSION.md`, `doc/EFFECT_REGRESSION_MATRIX.md`, and `doc/CROSS_PLATFORM_VALIDATION_MATRIX.md`.
