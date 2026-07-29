# WhatsCanvas Release Checklist

This checklist keeps releases aligned with the project's current goal: a lightweight, embeddable Canvas-style 2D renderer with reliable packaging and validation.

## Before Tagging

1. Update the version in `CMakeLists.txt`.
2. Update `include/wsc/Version.h`.
3. Update versioned `find_package` snippets in README and `doc/GETTING_STARTED_AS_LIBRARY.md`.
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

The default package scripts build a portable package with external FreeType disabled, so the artifact can be consumed without requiring the host project to define `Freetype::Freetype`. Set `WHATSCANVAS_PACKAGE_ENABLE_FREETYPE=1` only for dependency-aware packages.

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
git tag v0.1.x
git push origin master
git push origin v0.1.x
```

## GitHub Actions Checks

After pushing, verify these workflows:

- `Cross-Platform Validation`
  - `api-reference`
  - `unit` on Windows / Linux / macOS
  - `opengles-smoke`
  - `optional-font-stack`
  - `package-consumer`
- `Package WhatsCanvas`
  - package job on Windows / Linux / macOS
  - per-platform package consumer smoke before archive upload

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

## Release Notes

Release notes should call out:

- public API additions or breaking changes
- rendering behavior changes that update baselines
- text/font capability changes
- package target or dependency changes
- known platform limitations
