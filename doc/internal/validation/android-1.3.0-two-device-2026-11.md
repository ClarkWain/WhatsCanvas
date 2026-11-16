# Android 1.3.0 two-device validation — 2026-11-16

Tested the `1.3.0` branch at `7a0c53c` plus the local scaled-layer fix. The
Profile APK was built for all three configured ABIs with a temporary alternate
application ID, so the previously installed demo was preserved. The application
ID change was removed from the source tree after packaging.

| Device | Runtime | Feature scene and lifecycle |
| --- | --- | --- |
| Redmi K30 | Android 11, OpenGL ES 3.2, 1080 × 2400, DPR 2.75 | Portrait and landscape first frames, CJK/emoji, paths, clip/gradient, shadow, image sampling, Home/resume and rotation passed. |
| Pixel 3 | Android 12, OpenGL ES 3.2, 1080 × 2160, DPR 2.75 | The same checks passed. Its system font draws the keycap emoji differently; no glyph was missing. |

## Scaled `saveLayer` defect and fix

At fixed scene time 1.25 s, both devices showed only a fragment of the
Porter-Duff layer; the frosted-glass and inner-shadow cards were empty. The
desktop OpenGL scene at DPR 1 rendered correctly, while OpenGL and Software at
DPR 2 reproduced the missing content. `Canvas::saveLayer()` stored its logical
bounds, but `restoreLayer()` treated them as physical pixels when allocating
and compositing the offscreen target. The fix maps the bounds through the
current matrix when saving the layer.

Two exact-pixel tests for a translated layer at DPR 2 failed before the fix and
passed afterward in Software and OpenGL. The focused rendering suite passed
9/9, and the Software/OpenGL parity probe rendered 56/56 scenes with no delta
failures (`--require-gl --max-delta 2`). The Windows Release unit suite passed
69/69 when run serially. One parallel run produced a transient BatchMerge test
failure; that test passed both separately and in the serial suite. The repaired
APK was installed on both devices; all four compositing cards then rendered,
and Home/resume plus rotation still produced valid first frames.

| Device | Before | After |
| --- | --- | --- |
| Redmi K30 | [capture](assets/1.3.0-2026-11-16-redmi-compositing-before.png) | [capture](assets/1.3.0-2026-11-16-redmi-compositing-after.png) |
| Pixel 3 | [capture](assets/1.3.0-2026-11-16-pixel-compositing-before.png) | [capture](assets/1.3.0-2026-11-16-pixel-compositing-after.png) |

## Remaining performance issue

With the layer content actually rendered, the dynamic `compositing_stress`
scene stayed below the display rate. Five successive five-second callback
windows were 44.5, 44.7, 41.7, 38.4 and 38.1 FPS on the K30 while its display
used 50 Hz; the Pixel 3 measured 36.6, 32.8, 31.7, 32.0 and 32.0 FPS at 60 Hz.
Representative frame logs attributed roughly 6–11 ms CPU to backdrop replay
inside `saveLayer`, plus roughly 3–6 ms to flushing. Lowering the Pixel 3
display override to 720 × 1440 raised its later windows only to about 39 FPS;
the original display size was restored immediately afterward. The moving glass
rectangle changes the backdrop geometry and usually misses the exact cache.

These are render-callback rates and sampled CPU timings, not a distribution of
presented-frame latency or GPU time. This stress scene is heavier than the
feature scene and is not identical to the NanoVG control scene. Further work
should isolate backdrop command replay, GPU submission and presentation before
claiming a device-wide frame-rate target. iOS hardware was not tested here.
