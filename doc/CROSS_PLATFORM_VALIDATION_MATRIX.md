# WhatsCanvas Cross-Platform Validation Matrix

This matrix defines the validation surface for keeping the renderer portable across desktop OpenGL, OpenGLES-style builds, Vulkan, Metal, and text backends.

## Required Gates

| Target | Configure | Build | Test | Notes |
| --- | --- | --- | --- | --- |
| Windows desktop OpenGL | `cmake -S . -B build-win -DCMAKE_BUILD_TYPE=Debug` | `cmake --build build-win --config Debug` | `ctest --test-dir build-win -C Debug -L unit --output-on-failure` | Primary MSVC path and native bitmap compatibility path. |
| Linux desktop OpenGL | `cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug` | `cmake --build build-linux --config Debug` | Unit tests plus `WhatsCanvasOpenGLFilterPixelParityTests` under Xvfb/llvmpipe | Requires Mesa/OpenGL and X11 development packages for GLFW examples. |
| Linux via WSL2 | `scripts/wsl_linux_validation.ps1` | Script-owned | Script-owned | Windows-hosted Linux gate for GCC/CMake/unit coverage before CI runs. |
| macOS desktop OpenGL + Metal | `cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Debug` | `cmake --build build-macos --config Debug` | `ctest --test-dir build-macos -C Debug -L unit --output-on-failure`, then an explicit `-L metal` gate | Keeps Apple compiler/package layout green and executes the native Metal backend. |
| OpenGLES build and render | `scripts/opengles_build_smoke.*` | Script-owned | `WhatsCanvasOpenGLESFilterPixelParityTests` under Xvfb/Mesa EGL on Linux CI | Confirms GLES-specific compile/link assumptions and real filter shader output do not depend on desktop OpenGL. |
| Android OpenGLES host | `platforms/android/` Gradle + NDK build | `:app:assembleDebug`, `:app:assembleProfile`, `:app:assembleRelease` | `:app:lintDebug`, `:app:lintProfile`, plus API 33 AVD/manual device smoke | Debug validates diagnostics, Profile provides an installable `-O2 -DNDEBUG` performance target, and Release validates shipping configuration. Device smoke covers all packaged ABIs, UTF-8/CJK text, paths, round caps/dashes, images, portrait/landscape, and pause/resume context recreation. See [Android Integration](ANDROID_INTEGRATION.md). |
| WebAssembly + WebGL 2 host | `platforms/wasm/build.sh` with pinned Emscripten 4.0.22 | `WhatsCanvasWeb` | `platforms/wasm/test.sh` in clean headless Chrome | Reuses the canonical scene at the exact 786x377 and 393x759 logical viewports with a 3 DPR drawing buffer and 3 DPR PNG assertion. Generates all eight visual-parity captures and blocks on mixed CJK/emoji rendering, resize, visibility pause/resume, WebGL context recovery, cold reload, browser GL errors, and at-least-60-Hz requestAnimationFrame pacing. |
| Desktop platform host | `-DWHATSCANVAS_BUILD_DESKTOP_PLATFORM=ON` (default when `WhatsCanvas::OpenGL` is available) | `cmake --build <dir> --target WhatsCanvasDesktopHost` | Interactive smoke + `WhatsCanvasDesktopHost --scene=feature_showcase --dump-png=out.ppm --frames=1` and `--benchmark` on Windows/Linux/macOS | Portable GLFW + OpenGL 3.3 core host under `platforms/desktop/`. Mirrors the Android eight-card `feature_showcase` scene through the shared `IScene` contract, so cross-platform pixel diffs and per-scene wall-clock/GPU regressions can be built on the same content. |
| iOS Metal/CoreText host | `platforms/ios/WhatsCanvasDemo.xcodeproj` with Xcode 26.6 | `xcodebuild build` for simulator and generic iOS device | `xcodebuild test` on iPhone 17 Pro / iOS 26.5 | Builds the standalone Metal target, exercises native CoreText plus the Android-aligned feature scene, and verifies portrait/landscape, background/resume, and cold launch. Physical-device performance remains a release gate. |
| ASan + UBSan | Linux Debug build with `-fsanitize=address,undefined` | Full default target set | Blocking `ctest -L unit` | Detects heap/stack lifetime errors, out-of-bounds access, integer/shift UB, and invalid context/resource teardown paths. |
| Text/font-config fuzzing | Clang with `WHATSCANVAS_BUILD_FUZZERS=ON` | `WhatsCanvasTextAndFontConfigFuzzer` | 20,000 bounded libFuzzer runs over checked-in UTF-8 and Android XML seeds | Exercises malformed UTF-8, grapheme/fallback clustering, emoji detection, and AOSP/OEM font configuration parsing under ASan/UBSan. |
| Vulkan filter parity | `-DWHATSCANVAS_ENABLE_VULKAN=ON` | `WhatsCanvasVulkanFilterPixelParityTests` | Blocking run on lavapipe | Compares a deterministic composite filter scene with Software; the broader Vulkan label remains informational on hosted runners. |
| Metal backend on Apple hosts | Default on macOS/iOS (`WHATSCANVAS_ENABLE_METAL=ON`) | Native `xcode`/`ninja` build | `ctest --test-dir build -C Debug -L metal --output-on-failure` | 21 Metal test targets cover backend selection, command translation, text, single/multi clip, filters (Blur/InnerShadow/ColorAdjust/pixel-parity vs Software), blend modes, geometry (AA + polygon paths), gradients, render targets, mipmaps, GPU frame timing, raw DrawList seam, layer / paint / device lifecycle, and MTLTexture wrap. |
| Portable text backend | Default build | Default build | `WhatsCanvasFontManagerTests`, `WhatsCanvasTextBackendContractTests`, `WhatsCanvasVariableFontGoldenTests` | Covers font registration, fallback, atlas text, COLR/CPAL v0, CBDT/CBLC 2.0 and 3.0 PNG emoji, backend diagnostics, adapter fallback, exact/order-independent variation identity, and deterministic Roboto Flex axis pixels; Android runtime validation additionally covers Pixel 3/Android 12 CBDT 2.0 and API 33 Noto Color Emoji COLRv1 through the RGBA atlas. |
| Default OpenType shaping | Default configure (`WHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON`) | Build with vendored or system HarfBuzz | Unit tests | `third_party/harfbuzz` is preferred; absence must degrade to simple shaping with a diagnostic, not fail the build. |
| Default FreeType rasterizer | Default configure (`WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON`) | Build with vendored or system FreeType | Text contract tests | `third_party/freetype` is preferred; absence must degrade to `stb_truetype`, not fail the build. |
| Default combined font stack | Default configure | Linux CI installs HarfBuzz and FreeType development packages | `ctest -L text` plus `WhatsCanvasCoreBenchmarks` smoke | Validates shaping, glyph metrics/rasterization, atlas text, cache policy, and benchmark wiring in one configuration. |

## Text Backend Matrix

| Backend Slot | Status | Validation |
| --- | --- | --- |
| Portable glyph-atlas backend | Required | Unit and contract tests on every platform. |
| Windows native compatibility path | Platform optional | Enabled on Windows by `createBasicTextBackend` when native text is allowed. |
| DirectWrite adapter | Shipped (Windows) | Real IDWriteFactory-backed backend: shaping/metrics/rasterization, custom fonts (file+memory+lazy provider bridge), family-generation collection rebuilds, fallback chains, locale, letter spacing, real line breaking, underline/strikethrough, and grayscale/ClearType raster modes. Covered by `WhatsCanvasDirectWriteBackendTests` and `WhatsCanvasClearTypeCompositingTests`; selectable via `Canvas::setTextBackend(TextBackend::DirectWrite, ...)`. |
| CoreText adapter | Shipped (Apple) | Native measurement/rasterization, typesetter line breaking, system fallback, file/memory font registration, OpenType features/variations, decoration, and bounded bitmap caching are covered by `WhatsCanvasCoreTextBackendTests`; selectable through `Canvas::setTextBackend(TextBackend::CoreText)`. |
| HarfBuzz shaping adapter | Build-time optional | Factory and fallback diagnostics are tested with and without the library; vendored HarfBuzz is used when initialized. |
| FreeType rasterizer | Build-time optional | Glyph lookup, metrics, kerning, and alpha atlas rasterization are tested through the text backend contract tests. |

## Local Command

Use the helper script for the default local matrix slice:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\cross_platform_validation.ps1
```

On Unix-like shells:

```bash
./scripts/cross_platform_validation.sh
```

From Windows with WSL2 installed:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\wsl_linux_validation.ps1 -EnableOpenTypeShaping
```

These scripts intentionally run the portable checks that can execute on the current host. CI fans the same gates out to Windows, Linux, macOS, Android Gradle/NDK, sanitizer, and bounded fuzz runners.
