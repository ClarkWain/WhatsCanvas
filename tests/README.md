# WhatsCanvas Tests

This directory is the top-level home for WhatsCanvas validation beyond ad-hoc local commands.

## Current Entry Points

- `tests/compile/FeatureSnippets.cpp`: compile-only public API snippets.
- `tests/integration/vulkan_present/`: low-level Vulkan surface and swapchain
  validation kept separate from user-facing examples.
- `tests/visual/`: legacy showcase, analytic-AA, and image-filter capture
  harnesses used by smoke tests, documentation evidence, and pixel baselines.
- `WhatsCanvasAAShowcaseSmoke` and `WhatsCanvasImageFilterShowcaseSmoke`:
  headless capture gates for the visual harnesses, including fixed-size output
  on high-DPR hosts.

- `WhatsCanvasCoreDependencyBoundary`: prevents public headers, product source,
  platform adapters, and the root CMake file from acquiring reference-engine
  names or dependencies. Test-only Oracle tooling and comparison documentation
  remain outside the scanned product boundary.
- `ctest -C Debug -R ^WhatsCanvasGraphicsStateStackTests$ --output-on-failure`: lightweight unit executable covering `GraphicsStateStack` save/restore semantics and header-only `Path` behavior such as even-odd contains, stroke hit-testing, trim, and reverse.
- `ctest -C Debug -R ^WhatsCanvasTextLayoutTests$ --output-on-failure`: public `Canvas::layoutTextBox` unit coverage for paragraph ranges, alignment, line height, max-line clipping, and ellipsis.
- `ctest -C Debug -R ^WhatsCanvasUnicodeBidiConformanceTests$ --output-on-failure`: Unicode 17.0.0 `BidiTest.txt` conformance gate for the currently supported core Bidi profile. The default gate skips explicit embedding/isolate control cases and does not run `BidiCharacterTest.txt`.
- `build\Debug\WhatsCanvasUnicodeBidiConformanceTests.exe tests/unicode --exhaustive`: full Unicode 17.0.0 conformance run across `BidiTest.txt` and `BidiCharacterTest.txt`; the latest local exhaustive run passes all 861,948 cases with 0 skips and 0 failures.
- `WhatsCanvasTextUtilsTests`: includes rule-focused UAX #29 extended-grapheme coverage (Hangul, spacing marks, prepend characters, Indic conjuncts, emoji ZWJ sequences, regional indicators, and controls), complete UTF-16 cluster encoding for Android, and Unicode `Emoji_Presentation`/VS/ZWJ/skin-tone/flag/keycap/tag classification; generated Unicode property tables are committed for runtime use.
- `WhatsCanvasAndroidFontConfigTests`: parses modern and legacy Android font
  configuration, including product merges, aliases, style/TTC metadata, and
  locale-sensitive Simplified/Traditional Chinese fallback order. It also locks
  Skia-compatible `fallbackFor` isolation, exact-weight alias semantics,
  legacy `lang/variant`, duplicate-axis handling, and variable-font coordinates.
  Synthetic SFNT/TTC fixtures additionally verify intrinsic `OS/2/head/post`
  style recovery, explicit-config precedence, CSS3 style order, targeted
  fallback priority, and elegant/default/compact fallback passes. Disk-backed
  sanitized corpus fixtures cover the API 21/23/28/29/33/35 feature eras and
  vendor/product edge cases. Default emoji and structural emoji sequences must
  reorder the `und-Zsye` fallback even when VS16 is absent; see
  `fixtures/android_font_config/README.md`.
- `WhatsCanvasAndroidBuildContract`: prevents the Android app from silently
  disabling OpenGLES, FreeType, or HarfBuzz and verifies the three packaged ABI
  filters used by the sample.
- `WhatsCanvasAndroidFontOracleGolden`: runs the production Android parser
  through the versioned discovery/matching snapshot protocol. See
  `tools/font_oracle/README.md` for the offline golden gate and the independent
  fixed-revision Skia differential producer. External reference tests require
  `WHATSCANVAS_ENABLE_EXTERNAL_FONT_ORACLES=ON`; ordinary library builds expose
  no engine-specific cache settings. When configured, the separate
  `WhatsCanvasAndroidFontOracleSkia` test requires exact dual-engine equality;
  `WhatsCanvasAndroidFontOracleCorpusSkia` also locks strict and explicitly
  classified compatibility-extension behavior across the API/vendor corpus.
- `WhatsCanvasSkiaFontScannerGolden`: requires the separately built pinned
  Skia/FreeType scanner and opens the real bundled Roboto Flex fixture. It
  locks face/instance discovery, intrinsic style, and all variation-axis
  ranges/default positions independently of the virtual-path XML oracle.
- `WhatsCanvasSkiaAndroidFontManagerGolden`: runs the same real font through
  isolated `SkFontMgr_New_Android`, locking configured-family enumeration,
  intrinsic style fallback, Latin glyph selection, distinct zh-Hans and ja
  Source Han fallbacks, an `und-Zsye` color-emoji fallback, hidden
  fallback-family enumeration, and a still-missing CJK match.
- `WhatsCanvasSkiaFontRasterGolden`: renders the selected Latin, zh-Hans CJK,
  COLR/CPAL, and CBDT glyphs with pinned Skia/FreeType. It locks metrics, ink
  bounds/counts and engine-specific pixel hashes, and requires real colored
  pixels instead of treating successful font matching as render proof.
- `WhatsCanvasFontRasterGolden`: locks the same production-rasterizer scene.
  FreeType now takes priority for supported color glyphs, keeping COLR metrics
  aligned with the reference; the manual STB COLR path remains a fallback.
- `WhatsCanvasFontRasterDifferential`: compares the two engine snapshots by
  glyph, advance, bounds, ink area, and color classification without demanding
  byte-identical anti-aliasing. The common CBDT/CBLC index-format-1 plus
  image-format-17 PNG path is decoded through the existing stb image decoder
  when bundled FreeType cannot decode PNG itself.
- `WhatsCanvasAndroidFlagEmojiContract`: shapes the API 33 CN regional-indicator
  pair with HarfBuzz into one glyph and rasterizes its CBDT bitmap. The two
  component glyphs intentionally have no standalone bitmap metrics, so simple
  shaping cannot accidentally satisfy this contract.
- `WhatsCanvasFontManagerTests`: covers provider priority/generation, family and
  CSS-weight matching, cluster fallback, collection indices, and canonical
  variable-font face identity. Variation coordinate order is ignored while
  exact IEEE-754 values remain distinct. It also validates lazy source loading,
  failure memoization, callback re-entry outside internal locks, source
  replacement, family-scoped invalidation, and remote-font request scheduling,
  retry exhaustion, coverage selection, completion, transfer budgets, stale
  callback rejection, and content-fingerprint no-op/replacement semantics.
- `ctest -C Debug -R ^WhatsCanvasTextBackendContractTests$ --output-on-failure`: internal text backend contract coverage for font registration, fallback resolution, line breaks, glyph availability, and diagnostics. It also verifies that a remote provider queues a nonblocking miss, accepts host-supplied bytes, invalidates the cached miss, and shapes the downloaded face.
- `WhatsCanvasDirectWriteBackendTests`: includes a real lazy-provider bridge
  contract using bundled Source Serif variable bytes; attachment performs no
  load, first use materializes one family, and family invalidation reloads the
  native collection without hitting the old bitmap cache entry.
- `ctest --test-dir build -C Debug -R ^WhatsCanvasVariableFontGoldenTests$ --output-on-failure`:
  deterministic portable/software RGBA golden for bundled Roboto Flex at
  `wdth=50` and `wdth=150`. It locks both complete hashes and structural pixel
  differences. Set `WHATSCANVAS_UPDATE_VARIABLE_FONT_HASHES=1` only to print
  replacement hashes after an intentional raster change; the test never
  rewrites source automatically.
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
- `ctest -C Debug -R ^WhatsCanvasContextLifecycleTests$ --output-on-failure`: public `Canvas` context initialize, orderly finalize, no-delete abandon, resource release, recreation, local Picture-raster bounds, zero-budget bypass, and LRU pressure coverage.
- `ctest -C Debug -R ^WhatsCanvasGlyphAtlasTests$ --output-on-failure`: glyph lookup/atlas behavior plus repeated atlas-pressure and context-loss recreation stress.
- `cmake -S . -B build-fuzz -DWHATSCANVAS_BUILD_FUZZERS=ON` with Clang builds `WhatsCanvasTextAndFontConfigFuzzer`; seed corpora live in `tests/fuzz/corpus/` and CI runs a bounded ASan/UBSan smoke.
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
- `scripts/api_reference_check.bat` / `scripts/api_reference_check.sh`: verifies that `doc/public/reference/API_REFERENCE.md` matches the current `include/wsc/` public headers.
- `cmake --build build --target WhatsCanvasGenerateApiReference`: refreshes `doc/public/reference/API_REFERENCE.md` from `include/wsc/` after public header changes.
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
