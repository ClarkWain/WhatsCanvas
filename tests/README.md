# WhatsCanvas Tests

This directory is the top-level home for WhatsCanvas validation beyond ad-hoc local commands.

## Current Entry Points

- `ctest -C Debug -R ^WhatsCanvasGraphicsStateStackTests$ --output-on-failure`: lightweight unit executable covering `GraphicsStateStack` save/restore semantics and header-only `Path` behavior such as even-odd contains, stroke hit-testing, trim, and reverse.
- `ctest -C Debug -R ^WhatsCanvasTextLayoutTests$ --output-on-failure`: public `Canvas::layoutTextBox` unit coverage for paragraph ranges, alignment, line height, max-line clipping, and ellipsis.
- `ctest -C Debug -R ^WhatsCanvasUnicodeBidiConformanceTests$ --output-on-failure`: Unicode 17.0.0 `BidiTest.txt` conformance gate for the currently supported core Bidi profile. The default gate skips explicit embedding/isolate control cases and does not run `BidiCharacterTest.txt`.
- `build\Debug\WhatsCanvasUnicodeBidiConformanceTests.exe tests/unicode --exhaustive`: full Unicode 17.0.0 conformance run across `BidiTest.txt` and `BidiCharacterTest.txt`; the latest local exhaustive run passes all 861,948 cases with 0 skips and 0 failures.
- `WhatsCanvasTextUtilsTests`: includes rule-focused UAX #29 extended-grapheme coverage (Hangul, spacing marks, prepend characters, Indic conjuncts, emoji ZWJ sequences, regional indicators, and controls); generated Unicode property tables are committed for runtime use.
- `ctest -C Debug -R ^WhatsCanvasTextBackendContractTests$ --output-on-failure`: internal text backend contract coverage for font registration, fallback resolution, line breaks, glyph availability, and diagnostics.
- `ctest -C Debug -R ^WhatsCanvasTextRegressionTests$ --output-on-failure`: text fallback regression coverage for ASCII, Chinese, mixed Latin/CJK, uncovered codepoints, and declared fallback ranges.
- `ctest -C Debug -R ^WhatsCanvasRenderStatsTests$ --output-on-failure`: public `Canvas::getRenderStats` diagnostics API coverage, including filter pass, downsample, input-pixel, and pixel-pass accounting.
- `ctest --test-dir build-vulkan -C Debug -R ^WhatsCanvasVulkanImageFilterTests$ --output-on-failure`: real Vulkan GPU coverage for image/backdrop Gaussian blur, inner-shadow composition and clipping, Clamp/Decal edges, transparent-edge color safety, premultiplied translucent layers, per-axis adaptive downsampling, layer orientation, cropped clip coordinates, render statistics, and Software pixel parity.
- `WhatsCanvasOpenGLFilterPixelParityTests`,
  `WhatsCanvasOpenGLESFilterPixelParityTests`, and
  `WhatsCanvasVulkanFilterPixelParityTests`: render one deterministic composite
  scene and compare premultiplied RGBA output with the Software reference. The
  machine-readable `FILTER_PARITY` line reports max/mean error, bad-pixel ratio,
  worst coordinate/channel, and both hashes. Hosted Linux CI requires real
  Mesa contexts for GL/GLES and uses lavapipe for the blocking Vulkan baseline.
- `ctest -C Debug -R ^WhatsCanvasMatrixClipTests$ --output-on-failure`: public matrix mapping, clip bounds, quick reject, and transformed hit-test coverage.
- `ctest -C Debug -R ^WhatsCanvasPaintStateTests$ --output-on-failure`: public `Paint` state, gradient stop, path effect, color matrix, shadow, sampling, and blend-mode coverage.
- `ctest -C Debug -R ^WhatsCanvasStrokeTessellatorTests$ --output-on-failure`: internal stroke geometry compatibility, caps, joins, miter limits, closed paths, duplicate filtering, degenerate input, and deterministic robustness coverage.
- `ctest -C Debug -R ^WhatsCanvasImageResourceLifecycleTests$ --output-on-failure`: backend-neutral `Image` load, replace, update, external texture, reset, and move lifecycle coverage.
- `ctest -C Debug -R ^WhatsCanvasContextLifecycleTests$ --output-on-failure`: public `Canvas` context initialize, finalize, resource release, and recreation lifecycle coverage.
- `ctest -C Debug -L smoke --output-on-failure`: standard entry for the registered smoke/example script gates.
- `ctest -C Release -R ^WhatsCanvasPerformanceMatrixSmoke$ --output-on-failure`: end-to-end parameterized geometry/image/text benchmark smoke, including stable and dynamic-structure workloads plus JSON/CSV/Markdown report generation.
- `ctest -C Release -R ^WhatsCanvasPerformanceMatrixTests$ --output-on-failure`: unit coverage for workload generation, deterministic CLI construction, seed aggregation, and matrix report output.
- `ctest -C Release -R ^WhatsCanvasCrossLibraryMatrixTests$ --output-on-failure`: unit coverage for cross-library matrix generation, complete parameter forwarding, confidence verdicts, and aggregate exports.
- `ctest -C Debug -R ^WhatsCanvasApiReferenceCheck$ --output-on-failure`: generated public API reference freshness check.
- `ctest -C Debug -R ^WhatsCanvasVersionConsistencyCheck$ --output-on-failure`: verifies CMake, public version macros, docs, and release workflow version handling stay synchronized.
- `ctest -C Debug -R ^WhatsCanvasPackageConsumerSmoke$ --output-on-failure`: package/export smoke that builds the external CMake consumer under `tests/package_consumer`.
- `scripts/smoke_test.bat` / `scripts/smoke_test.sh`: fixed-time first-frame smoke gate.
- `scripts/clip_path_smoke.bat` / `scripts/clip_path_smoke.sh`: stacked non-rect `clipPath` smoke gate.
- `scripts/examples_smoke.bat` / `scripts/examples_smoke.sh`: independent example build gate.
- `scripts/validation_scene_smoke.bat` / `scripts/validation_scene_smoke.sh`: six-scene render smoke gate covering text, images, gradients/effects, clipping, transforms, and saveLayer.
- `scripts/opengles_build_smoke.bat` / `scripts/opengles_build_smoke.sh`:
  OpenGLES-only configure/build smoke gate; the shell path also builds the
  GLES pixel-parity executable that CI runs through EGL/Xvfb.
- `scripts/regression_smoke.bat` / `scripts/regression_smoke.sh`: strict local pixel-baseline gate.
- `scripts/text_pixel_regression.bat` / `scripts/text_pixel_regression.sh`: font-only pixel regression for the `font-regression` and `text-showcase` scenes against `tests/baselines/text/*.ppm`; set `WHATSCANVAS_UPDATE_TEXT_BASELINES=1` to refresh baselines, or `WHATSCANVAS_TEXT_REGRESSION_SCENES=font-regression` to narrow the scene list locally.
- `scripts/compare_ppm_fuzzy.py`: binary P6 PPM comparison helper for driver-sensitive visual baselines.
- `scripts/api_reference_check.bat` / `scripts/api_reference_check.sh`: verifies that `doc/API_REFERENCE.md` matches the current `include/wsc/` public headers.
- `cmake --build build --target WhatsCanvasGenerateApiReference`: refreshes `doc/API_REFERENCE.md` from `include/wsc/` after public header changes.
- `cmake --build build --target WhatsCanvasCheckApiReference`: checks generated API reference freshness through the configured Python interpreter.
- `scripts/version_consistency_check.bat` / `scripts/version_consistency_check.sh`: verifies version declarations are synchronized across CMake, public headers, docs, and release packaging.
- `cmake --build build --target WhatsCanvasCheckVersionConsistency`: checks release metadata consistency through the configured Python interpreter.
- `scripts/package_consumer_smoke.bat` / `scripts/package_consumer_smoke.sh`: builds `tests/package_consumer` against the installed package to verify `find_package(WhatsCanvas CONFIG REQUIRED)` and exported targets.
- `cmake --build build --target WhatsCanvasCheckPackageConsumer`: runs the external package consumer smoke through the configured build tree.
- `scripts/release_preflight.bat` / `scripts/release_preflight.sh`: fast local release preflight that runs API reference freshness, version consistency, a Debug build, unit tests, and the registered API/version/package CTest gates.

## Intended Growth

- expand unit coverage beyond the current state-stack and basic path semantics tests into clip/query helpers and text measurement helpers;
- render-scene fixtures that can be driven by `ctest` without recursively rebuilding the whole tree;
- backend-consistency tests once alternate backends land;
- stress tests for resize, saveLayer, clip nesting, and resource lifetime.
