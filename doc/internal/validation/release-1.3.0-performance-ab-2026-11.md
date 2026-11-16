# 1.3.0 release performance A/B — 2026-11-16

Compared the `v1.2.1` tag with the proposed `1.3.0` source on the same Windows
host (Intel Family 6 Model 158 CPU, MSVC 19.43, Release, Software + desktop
OpenGL, FreeType and HarfBuzz enabled). Each scene ran in three fresh processes
per version with 5 warm-up and 30 measured frames. Runs were interleaved
old/new/new/old/old/new; the table uses the median of process medians. Pixel
hashes were retained for every process. Readback and hashing are outside the
synchronized frame-time interval.

| Scene and size | 1.2.1 median | 1.3.0 median | Change | Pixel quality |
| --- | ---: | ---: | ---: | --- |
| Software `clip_layers`, 1920 × 1080 | 612.18 ms | 79.79 ms | −87.0% | Exact frame hash `038d261d2869a07e` on both versions. |
| OpenGL `shadow_grid`, 1920 × 1080 | 18.08 ms | 2.98 ms | −83.5% | RGB PPM captures are byte-identical. RGBA hashes differ because shadow alpha blending was corrected; 1.3.0 OpenGL vs Software RGB max delta is 2, mean 0.02663. |
| Software `frosted_glass`, 960 × 540 | 239.70 ms | 139.12 ms | −42.0% | RGB max delta 2, mean 0.04903, with no pixel above delta 4. |
| OpenGL `text_churn`, 960 × 540 | 1.78 ms | 1.71 ms | −3.8% | Exact frame hash `8e0605b49abc5ea9`; cold-frame results below. |
| OpenGL `image_grid`, 1920 × 1080 | 0.261 ms | 0.263 ms | +0.7% | Exact frame hash `432ad28b33a51375`; within run variation. |
| Software `rounded_ui`, 960 × 540 | 27.84 ms | 27.88 ms | +0.1% | Exact frame hash `2b707d5f6743bd31`; within run variation. |

Fresh-process `text_churn` cold frames were 657.46, 411.91 and 406.40 ms on
1.2.1 versus 49.53, 38.44 and 82.88 ms on 1.3.0. The 82.88 ms run spent
80.04 ms recording. Five additional
isolated 1.3.0 processes measured 37.80–38.19 ms with the same hash. The
three-run median improved, but the documented per-run 60 ms cold gate was not
met by that first interleaved sample; retain the exception rather than claiming
zero variation.

One lint experiment replaced the Software per-pixel round-half-up expression
with `std::lround` and raised the 1080p `clip_layers` median to 221.60 ms.
Restoring the original expression measured 79.78 ms with the same frame hash.
The release keeps the fast expression with a scoped clang-tidy rationale.

The final review found a Software backdrop cache invalidation bug: resizing an
antialiased rectangle could retain the same topology fingerprint and reuse stale
filtered pixels. A regression test reproduced the stale frame before the fix and
passed after shared geometry gained a precomputed fingerprint of its vertex
positions. Stable geometry still hits the cache. A 5-warmup/30-frame Release
check before/after this fix measured `clip_layers` 79.72→79.15 ms,
`frosted_glass` 139.00→138.16 ms, and `rounded_ui` 27.96→27.44 ms; all three
retained exact frame hashes. These small differences are within run variation.

## Release decision boundary

No quality-comparable control scene regressed by 10% or more in the process
median, and the targeted slow scenes improved. The shadow and frosted-glass
timing comparisons include intentional rendering changes, so their quality
checks above are part of the acceptance evidence. Software `clip_layers` and
`frosted_glass` remain well above 16.7 ms and do not establish 60 FPS. Android
dense-shadow GPU timing, iOS physical-device retest, and the Spider Solitaire
long-session slow-frame investigation remain separate work.
