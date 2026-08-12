# WhatsCanvas Cross-Platform Validation Matrix

This matrix defines the validation surface for keeping the renderer portable across desktop OpenGL, OpenGLES-style builds, and text backends.

## Required Gates

| Target | Configure | Build | Test | Notes |
| --- | --- | --- | --- | --- |
| Windows desktop OpenGL | `cmake -S . -B build-win -DCMAKE_BUILD_TYPE=Debug` | `cmake --build build-win --config Debug` | `ctest --test-dir build-win -C Debug -L unit --output-on-failure` | Primary MSVC path and native bitmap compatibility path. |
| Linux desktop OpenGL | `cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug` | `cmake --build build-linux --config Debug` | Unit tests plus `WhatsCanvasOpenGLFilterPixelParityTests` under Xvfb/llvmpipe | Requires Mesa/OpenGL and X11 development packages for GLFW examples. |
| Linux via WSL2 | `scripts/wsl_linux_validation.ps1` | Script-owned | Script-owned | Windows-hosted Linux gate for GCC/CMake/unit coverage before CI runs. |
| macOS desktop OpenGL | `cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Debug` | `cmake --build build-macos --config Debug` | `ctest --test-dir build-macos -C Debug -L unit --output-on-failure` | Keeps Apple compiler and package layout green. |
| OpenGLES build and render | `scripts/opengles_build_smoke.*` | Script-owned | `WhatsCanvasOpenGLESFilterPixelParityTests` under Xvfb/Mesa EGL on Linux CI | Confirms GLES-specific compile/link assumptions and real filter shader output do not depend on desktop OpenGL. |
| Vulkan filter parity | `-DWHATSCANVAS_ENABLE_VULKAN=ON` | `WhatsCanvasVulkanFilterPixelParityTests` | Blocking run on lavapipe | Compares a deterministic composite filter scene with Software; the broader Vulkan label remains informational on hosted runners. |
| Metal backend on Apple hosts | Default on macOS/iOS (`WHATSCANVAS_ENABLE_METAL=ON`) | Native `xcode`/`ninja` build | `ctest --test-dir build -C Debug -L metal --output-on-failure` | 22 Metal test files cover backend selection, command translation, text, single/multi clip, filters (Blur/InnerShadow/ColorAdjust/pixel-parity vs Software), blend modes, geometry (AA + polygon paths), gradients, render targets, mipmaps, GPU frame timing, raw DrawList seam, layer / paint / device lifecycle, and MTLTexture wrap. Parity with the Vulkan test surface. |
| Portable text backend | Default build | Default build | `WhatsCanvasTextBackendContractTests` | Covers font registration, fallback, atlas text, COLR/CPAL v0, backend diagnostics, and adapter fallback. |
| Default OpenType shaping | Default configure (`WHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON`) | Build with vendored or system HarfBuzz | Unit tests | `third_party/harfbuzz` is preferred; absence must degrade to simple shaping with a diagnostic, not fail the build. |
| Default FreeType rasterizer | Default configure (`WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON`) | Build with vendored or system FreeType | Text contract tests | `third_party/freetype` is preferred; absence must degrade to `stb_truetype`, not fail the build. |
| Default combined font stack | Default configure | Linux CI installs HarfBuzz and FreeType development packages | `ctest -L text` plus `WhatsCanvasCoreBenchmarks` smoke | Validates shaping, glyph metrics/rasterization, atlas text, cache policy, and benchmark wiring in one configuration. |

## Text Backend Matrix

| Backend Slot | Status | Validation |
| --- | --- | --- |
| Portable glyph-atlas backend | Required | Unit and contract tests on every platform. |
| Windows native compatibility path | Platform optional | Enabled on Windows by `createBasicTextBackend` when native text is allowed. |
| DirectWrite adapter | Shipped (Windows) | Real IDWriteFactory-backed backend: shaping/metrics/rasterization, custom fonts (file+memory), fallback chains, locale, letter spacing, real line breaking, underline/strikethrough, and grayscale/ClearType raster modes. Covered by `WhatsCanvasDirectWriteBackendTests` and `WhatsCanvasClearTypeCompositingTests`; selectable via `Canvas::setTextBackend(TextBackend::DirectWrite, ...)`. |
| CoreText adapter | Adapter slot reserved | Capability query and unavailable-adapter diagnostic are tested until implementation lands. |
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

These scripts intentionally run the portable checks that can execute on the current host. CI fans the same gates out to Windows, Linux, and macOS runners.
