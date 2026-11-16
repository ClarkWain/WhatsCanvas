# WhatsCanvas 1.3.0

WhatsCanvas 1.3.0 improves rendering consistency and reduces several measured
desktop frame costs. The public drawing API, CMake target names, and package
layout are unchanged from 1.2.1.

## Rendering fixes

- Correct `saveLayer` bounds under device-pixel scaling and transforms. The
  former logical-versus-device coordinate mismatch could leave clipped or
  missing Porter-Duff content, frosted glass, and inner shadows on high-DPR
  surfaces. Software, OpenGL, OpenGL ES, and two Android devices have regression
  coverage for this path.
- Align Software half-pixel rectangle edges with OpenGL and fix the alpha
  blend factors used for OpenGL shadows.
- Correct Software backdrop cache invalidation when an antialiased shape changes
  size between frames. A reused canvas now matches a fresh render pixel for pixel.
- Increase spacing around mixed CJK/emoji text in the cross-platform feature
  scene so its explanation does not overlap on device fonts.

## Performance

- Limit OpenGL-family shadow blur and composite work to the transformed
  silhouette and Gaussian support. A fresh 1.2.1/1.3.0 A/B measured 18.08 ms
  before and 2.98 ms after at 1920 × 1080 on the same Windows host; RGB
  captures were byte-identical while the shadow alpha fix corrected output.
- Reduce Software clip and backdrop work with bounded coverage reuse, safe
  opaque-blur downsampling, and parallel processing of independent image rows.
  The 16-panel clipped-layer scene fell from 612.18 to 79.79 ms median at
  1920 × 1080 on the same machine, with an unchanged frame hash.
- Avoid a fresh DirectWrite system-font collection on the first default font
  lookup. The three-process first-text median fell from 411.91 to 49.53 ms in
  the fresh A/B. One interleaved 1.3.0 run reached 82.88 ms; five later isolated
  fresh processes measured 37.80–38.19 ms with the same frame hash.

These numbers describe specific desktop workloads. Large continuously redrawn
Software surfaces, including the clipped-layer scene above, remain outside a
16.7 ms frame budget. The Android compositing stress scene also remains below
the display rate on the tested phones when all effects are rendered. The
Spider Solitaire example's long-session slow frames remain under investigation.

## Validation and compatibility

The 56-scene Software/OpenGL probe completed with no render or pixel-threshold
failures. CI covers Windows, Linux, macOS, Android SDK packaging, iOS SDK
packaging, Web, sanitizers, bounded fuzzing, and OpenGL ES filter pixels. Redmi
K30 and Pixel 3 device captures verify the high-DPR layer fix in portrait;
both devices were also checked through rotation and a landscape feature scene.
This release does not claim a new device-wide FPS target; a dedicated
Android dense-shadow GPU benchmark and a physical iOS retest were not completed.

The shipped assets retain their existing structure and target names:

- `whatscanvas-win64-release-1.3.0.zip`
- `whatscanvas-linux-x64-release-1.3.0.zip`
- `whatscanvas-macos-universal-release-1.3.0.zip`
- `whatscanvas-android-release-1.3.0.aar` (Prefab SDK)
- `whatscanvas-ios-release-1.3.0.zip` (XCFramework)

See the [changelog](../../../../CHANGELOG.md) for the detailed change list and
the [API stability guide](../../../public/reference/API_STABILITY.md) for
support boundaries.
