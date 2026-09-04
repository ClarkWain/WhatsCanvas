# Representative Mobile Hardware Sign-off — September 2026

Status: **complete for the stable v1 product scope**.

This record consolidates the physical-device evidence that was previously
spread across platform guides, troubleshooting notes, performance reports, and
the changelog. Representative hardware validation is complete; broader device
coverage is useful compatibility work, not an unfinished v1 capability.

## Android

| Device | Environment | Verified behavior | Result |
| --- | --- | --- | --- |
| Google Pixel 3 | Android 12, Adreno 630, OpenGL ES | Cold start, CJK and emoji including CBDT 2.0, HiDPI fallback, retained Picture playback, pause/resume, and display-rate pacing | Passed; steady-state callback rate was approximately 60.7–60.8 FPS |
| Redmi K30 | Android 11 / MIUI 12.5, Adreno, OpenGL ES 3.2 | Full feature scene, gradients, system-font configuration, retained/raster cache, pause/resume, dynamic rendering, and OEM 50/60 Hz modes | Passed; rendering followed the active 49.7–59.6 Hz display mode |
| Mi MIX 2 and HTC U Ultra | 1080 × 2160 and 1440 × 2560 | Compositing stress, command compilation, saveLayer cache behavior, and fixed-time pixel output | Passed; the optimized path improved frame rate while preserving the recorded pixel hash |

The reproducible integration checklist and device details are in
[`ANDROID_INTEGRATION.md`](../../public/platforms/ANDROID_INTEGRATION.md). The
optimization measurements are retained in the
[`performance implementation log`](../../archive/implementation/performance-optimization-log.md),
and the Spider Solitaire device comparison is documented in its
[`Android performance guide`](../../../examples/game/spider_solitaire/android/PERFORMANCE.md).

## iOS

| Device | Environment | Verified behavior | Result |
| --- | --- | --- | --- |
| iPhone 12 | A14 GPU, iOS 18.7.8, Metal + CoreText | Complete feature scene, CJK and emoji, clipping, gradients, shadows, physical presentation, Debug Metal API Validation, orientation, background/resume, and cold start | Passed; Release pacing remained at 59.2–59.9 FPS |

The physical-device failures found during bring-up and their final regression
results are recorded in
[`DEVICE_RENDERING_TROUBLESHOOTING.md`](../../../platforms/ios/DEVICE_RENDERING_TROUBLESHOOTING.md).
The public integration summary is
[`IOS_BUILD_NOTES.md`](../../public/platforms/IOS_BUILD_NOTES.md).

## Scope of this sign-off

- The stable v1 target required representative Android and iOS hardware, not a
  promise covering every OS, vendor, GPU, thermal condition, or host app.
- Simulator/emulator and CI checks remain the repeatable development gates.
  The August visual-parity run is retained in
  [`visual-parity-record-2026-08.md`](visual-parity-record-2026-08.md).
- Repeat the relevant physical-device checks when a release changes mobile
  presentation, lifecycle, shaders, text/font handling, packaging, or frame
  scheduling. A release with no such changes does not reopen the completed
  product capability.
