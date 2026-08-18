# WhatsCanvas Release Checklist

This checklist keeps releases aligned with the project's current goal: a lightweight, embeddable Canvas-style 2D renderer with reliable packaging and validation.

## Before Tagging

1. Update the version in `CMakeLists.txt`.
2. Update `include/wsc/Version.h`.
3. Update Android `versionCode` / `versionName`, CHANGELOG, and versioned
   `find_package` snippets in both READMEs and `doc/GETTING_STARTED_AS_LIBRARY.md`.
4. Run the fast metadata checks:

```bat
cmake --build build --target WhatsCanvasCheckApiReference
cmake --build build --target WhatsCanvasCheckVersionConsistency
cmake --build build --target WhatsCanvasCheckPerformanceClaims
cmd /c scripts\api_reference_check.bat
cmd /c scripts\version_consistency_check.bat
```

5. Run the package consumer smoke:

```bat
cmake --build build --target WhatsCanvasCheckPackageConsumer
cmd /c scripts\package_consumer_smoke.bat
```

The default package scripts include the bundled FreeType and HarfBuzz targets
used by the GL-family libraries. Set `WHATSCANVAS_PACKAGE_ENABLE_FREETYPE=0`
and pass `-DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=OFF` through
`WHATSCANVAS_CMAKE_EXTRA_ARGS` only for a deliberately reduced text package.

6. Run the normal validation set appropriate for the change:

```bat
ctest --test-dir build -C Debug -L unit --output-on-failure
ctest --test-dir build -C Debug -R "^(WhatsCanvasApiReferenceCheck|WhatsCanvasVersionConsistencyCheck|WhatsCanvasPackageConsumerSmoke)$" --output-on-failure
```

For rendering or text changes, also run the relevant smoke/pixel gates from `README.md` and `doc/REGRESSION_BASELINES.md`.

For the common fast path, the metadata, Debug unit, and package-consumer checks can be run together:

```bat
cmd /c scripts\release_preflight.bat
```

## Tagging

Use a tag matching the package workflow pattern:

```sh
git tag vX.Y.Z
git push origin master
git push origin vX.Y.Z
```

## GitHub Actions Checks

After pushing, verify these workflows:

- `Cross-Platform Validation`
  - `api-reference`
  - `unit` on Windows / Linux / macOS
  - `opengles-smoke`
  - `default-font-stack`
  - `package-consumer`
- `Package WhatsCanvas`
  - package job on Windows / Linux / macOS
  - per-platform package consumer smoke before archive upload
  - Android Profile demo build, `lintProfile`, and APK upload

## Artifact Sanity Check

Download each workflow artifact and verify the expected layout:

```text
include/wsc/
lib/
lib/cmake/WhatsCanvas/
```

Then point an external project at the extracted package:

```sh
cmake -S tests/package_consumer -B build-package-consumer \
  -DCMAKE_PREFIX_PATH=/path/to/extracted/package \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-package-consumer --config Release
```

Install the Android release asset on a representative device and verify that
its package version is correct, the feature scene renders CJK/color emoji, the
frame callback follows the active display mode, and pause/resume recreates GPU
resources without changing completed content. The APK is intentionally
debug-signed for evaluation; do not treat it as a production signing artifact.

## Release Notes

Release notes should call out:

- public API additions or breaking changes
- rendering behavior changes that update baselines
- text/font capability changes
- package target or dependency changes
- known platform limitations
