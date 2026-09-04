# WhatsCanvas Release Checklist

> Maintainer procedure. Keep this version-neutral; store evidence tied to a
> completed version under `doc/archive/releases/<version>/`.

This is a recurring release procedure, not a list of unfinished product
capabilities. The stable-v1 representative Android/iOS hardware target is
complete; see
[`mobile-hardware-signoff-2026-09.md`](../validation/mobile-hardware-signoff-2026-09.md).
Repeat a hardware check when the release changes the corresponding platform
path.

This checklist keeps releases aligned with the project's current goal: a lightweight, embeddable Canvas-style 2D renderer with reliable packaging and validation.

Every release must have objective evidence for its blocking criteria. Historical
1.0 criteria and evidence are retained in `doc/archive/releases/1.0/`; create a
version-specific evidence record for each new release instead of modifying the archive.

## Before Tagging

1. Update the version in `CMakeLists.txt`.
2. Update `include/wsc/Version.h`.
3. Update Android `versionCode` / `versionName`, CHANGELOG, and versioned
   `find_package` snippets in both READMEs and `doc/public/getting-started/GETTING_STARTED_AS_LIBRARY.md`.
   The Android `versionCode` convention is `major * 10000 + minor * 100 + patch`.
4. Review the release-specific dependency audit and confirm official
   packages contain `LICENSE`, `THIRD_PARTY_NOTICES.md`, and bundled dependency licenses.
5. Run the fast metadata checks:

```bat
cmake --build build --target WhatsCanvasCheckApiReference
cmake --build build --target WhatsCanvasCheckVersionConsistency
cmake --build build --target WhatsCanvasCheckPerformanceClaims
cmd /c scripts\api_reference_check.bat
cmd /c scripts\version_consistency_check.bat
```

6. Run the package consumer smoke:

```bat
cmake --build build --target WhatsCanvasCheckPackageConsumer
cmd /c scripts\package_consumer_smoke.bat
```

The default package scripts include the bundled FreeType and HarfBuzz targets
used by the GL-family libraries. Set `WHATSCANVAS_PACKAGE_ENABLE_FREETYPE=0`
and pass `-DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=OFF` through
`WHATSCANVAS_CMAKE_EXTRA_ARGS` only for a deliberately reduced text package.

7. Run the normal validation set appropriate for the change:

```bat
ctest --test-dir build -C Debug -L unit --output-on-failure
ctest --test-dir build -C Debug -R "^(WhatsCanvasApiReferenceCheck|WhatsCanvasVersionConsistencyCheck|WhatsCanvasPackageConsumerSmoke)$" --output-on-failure
```

For rendering or text changes, also run the relevant smoke/pixel gates from
`README.md` and `doc/public/validation/VISUAL_REGRESSION.md`.

For the common fast path, API/version/performance metadata, Debug unit, and
package-consumer checks can be run together:

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
  - `android` AAR packaging and `ios-sdk` XCFramework packaging
  - `default-font-stack`
  - `package-consumer`
- `Package WhatsCanvas`
  - package job on Windows / Linux / macOS
  - per-platform package consumer smoke before archive upload
  - Android Prefab AAR build/layout verification plus Profile demo build and lint
  - iOS XCFramework device/simulator build and layout verification

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

Verify the mobile assets with the same structural gate used in CI:

```sh
python3 scripts/verify_mobile_release_artifact.py \
  --android-aar whatscanvas-android-release-X.Y.Z.aar
python3 scripts/verify_mobile_release_artifact.py \
  --ios-zip whatscanvas-ios-release-X.Y.Z.zip
```

Verify an extracted desktop install tree, including its version and licenses:

```sh
python3 scripts/verify_desktop_release_artifact.py --package-dir /path/to/package
```

The Android AAR must expose the `whatscanvas::whatscanvas` Prefab target and
compile in a clean consuming application for every distributed ABI. Install a
locally built Profile demo on a representative device and verify CJK/color
emoji, pacing, and pause/resume; the demo APK is not a Release asset.

The iOS XCFramework must link in a clean Objective-C++ consumer for both a
generic device and simulator destination with Metal, Foundation, QuartzCore,
CoreGraphics, CoreText, and UIKit. Application signing remains host-owned.

## Release Notes

Release notes should call out:

- public API additions or breaking changes
- rendering behavior changes that update baselines
- text/font capability changes
- package target or dependency changes
- known platform limitations
