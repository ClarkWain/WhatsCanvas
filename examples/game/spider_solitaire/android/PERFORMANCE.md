# Android Spider performance verification

The reusable explanation of the 1 FPS drag regression, cache architecture,
animation choices, native Android Canvas comparison, and final device results
is published in
[`doc/ANDROID_INTERACTIVE_PERFORMANCE.md`](../../../doc/ANDROID_INTERACTIVE_PERFORMANCE.md).
This file is the operational guide for reproducing this module's measurements.

The benchmark uses one physical gesture to cover all three expensive states:
select a face-up card, drag it continuously across the table, and release it
over an invalid destination so it snaps back.

From PowerShell, with one Android device connected:

```powershell
cd platforms/android/spider
.\verify_drag_performance.ps1
```

The script builds and installs the debug APK, waits for cold-start shader and
picture-cache warm-up, sends the same five-second ADB swipe, and parses the
native `DRAG_PERF` record. It fails unless the measured drag is at least 45 FPS
and its slowest frame is at most 250 ms. Thresholds can be overridden when
qualifying a different device class:

```powershell
.\verify_drag_performance.ps1 -MinimumFps 50 -MaximumFrameMs 200 -SkipBuild
```

The in-app sample includes the gesture duration, rendered-frame count, input
move count, average CPU frame time, maximum CPU frame time, and number of
frames over 20 ms. This avoids relying on Android `gfxinfo`, which does not
report frames drawn directly by `GLSurfaceView`.

## Native Canvas control

The APK also contains an Android `Canvas` control renderer. It deliberately
redraws the full 54-card tableau on every VSYNC using only two shared Bitmaps:
one face base and one card back. Ranks and suits are separate text overlays;
there are no per-card image allocations.

Run the same gesture against both renderers:

```powershell
.\compare_renderers.ps1
```

The control is selected internally with the `renderer=native` Activity extra;
normal launches still use WhatsCanvas. The Android WhatsCanvas host uses one
shared 1024x1024 card atlas. It is baked once from the original vector UI with
the software backend, preserving gradients, font glyphs, card-back ornaments,
and suit paths. Cards keep only logical rank/suit/state and no per-card texture
is allocated. The background is also baked once in viewport coordinates so the
radial gradient remains centered.

On the Mi MIX 2 reference device after enabling the atlas, the same 5-second
gesture measured 59.8 FPS / 8.42 ms average work / 23.26 ms maximum work for
WhatsCanvas, versus 60.0 FPS / 0.56 ms average `onDraw` work / 1.81 ms maximum
`onDraw` work for the native control. The CPU timing boundaries differ, but the
end-to-end rendered frame counts show both paths now sustain the display rate.

The final Spider build was also checked during dealing. Moving stock cards and
press feedback are drawn in the dynamic layer from the atlas; they no longer
invalidate the large header/table raster caches. Visual counting remained at
60 FPS, with only an isolated approximately 23 ms edge frame on the second
deal. Treat these figures as a reference-device result, not a universal device
claim.
