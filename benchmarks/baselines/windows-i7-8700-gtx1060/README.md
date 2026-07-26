# WhatsCanvas Performance Summary

This is a reproducible single-machine reference, not a cross-library ranking. Compare timing only against runs with matching environment metadata.

## Environments

| Backend | Environment |
|---|---|
| opengl | WhatsCanvasPerformanceSuite 0.1.16 @ 4db6118d73d0; Intel64 Family 6 Model 158 Stepping 10, GenuineIntel; NVIDIA GeForce GTX 1060 3GB/PCIe/SSE2 (3.3.0 NVIDIA 560.94); Release, standard, 960x540, 30+5 frames |
| software | WhatsCanvasPerformanceSuite 0.1.16 @ 4db6118d73d0; Intel64 Family 6 Model 158 Stepping 10, GenuineIntel; cpu (n/a); Release, standard, 960x540, 30+5 frames |
| vulkan | WhatsCanvasPerformanceSuite 0.1.16 @ 4db6118d73d0; Intel64 Family 6 Model 158 Stepping 10, GenuineIntel; NVIDIA GeForce GTX 1060 3GB (driver:2350350336;api:1.3.280); Release, standard, 960x540, 30+5 frames |

## Results

| Backend | Scene | Median | p95 | FPS | Cold | Record | Submit | Peak RSS | Pixel hash |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| opengl | solid_rects | 34.084 ms | 37.772 ms | 29.3 | 42.238 ms | 1.277 ms | 32.718 ms | 113.0 MiB | `532a1d781a27158d` |
| opengl | rounded_ui | 10.136 ms | 10.591 ms | 98.7 | 15.742 ms | 2.663 ms | 7.404 ms | 113.7 MiB | `793da3117f83c137` |
| opengl | path_cached | 16.277 ms | 18.334 ms | 61.4 | 22.940 ms | 3.072 ms | 13.174 ms | 113.0 MiB | `cb89eff7029a45d6` |
| opengl | path_churn | 16.594 ms | 17.731 ms | 60.3 | 22.628 ms | 3.612 ms | 12.983 ms | 113.8 MiB | `cb89eff7029a45d6` |
| opengl | image_grid | 191.084 ms | 202.133 ms | 5.2 | 245.092 ms | 0.581 ms | 190.468 ms | 119.5 MiB | `c39889e9e742f3ad` |
| opengl | clip_layers | 17.620 ms | 19.421 ms | 56.8 | 44.880 ms | 14.111 ms | 3.485 ms | 114.3 MiB | `77cf9c41b4fe5fd8` |
| opengl | shadow_grid | 11.764 ms | 12.405 ms | 85.0 | 114.534 ms | 0.925 ms | 10.744 ms | 112.4 MiB | `c0ef178d4f90e1fb` |
| opengl | text_cached | 9.924 ms | 11.470 ms | 100.8 | 41.121 ms | 2.337 ms | 7.550 ms | 143.8 MiB | `119f40f0cbe5c696` |
| opengl | text_churn | 12.860 ms | 14.380 ms | 77.8 | 52.311 ms | 4.704 ms | 7.979 ms | 143.0 MiB | `ecf9629486bf70e0` |
| opengl | frosted_glass | 10.082 ms | 11.210 ms | 99.2 | 140.466 ms | 8.071 ms | 2.030 ms | 113.2 MiB | `9620a19f53703cd3` |
| opengl | inner_shadow | 24.800 ms | 25.855 ms | 40.3 | 144.359 ms | 20.571 ms | 4.119 ms | 114.2 MiB | `b6547bc24fa666e3` |
| software | solid_rects | 23.942 ms | 24.452 ms | 41.8 | 23.933 ms | 1.427 ms | 22.476 ms | 15.2 MiB | `532a1d781a27158d` |
| software | rounded_ui | 36.932 ms | 44.328 ms | 27.1 | 39.990 ms | 3.223 ms | 33.686 ms | 15.9 MiB | `b1f2d33f381c369b` |
| software | path_cached | 28.107 ms | 33.190 ms | 35.6 | 32.117 ms | 3.359 ms | 24.748 ms | 15.6 MiB | `c5a09678c3f90d6f` |
| software | path_churn | 29.092 ms | 31.367 ms | 34.4 | 28.972 ms | 4.335 ms | 24.734 ms | 15.9 MiB | `c5a09678c3f90d6f` |
| software | image_grid | 94.119 ms | 102.523 ms | 10.6 | 90.343 ms | 0.673 ms | 93.437 ms | 19.4 MiB | `c01bc60d9e4a67b4` |
| software | clip_layers | 136.746 ms | 147.850 ms | 7.3 | 149.334 ms | 71.614 ms | 65.139 ms | 20.7 MiB | `e543b5bf7e7887ab` |
| software | shadow_grid | 1003.717 ms | 1019.244 ms | 1.0 | 1009.125 ms | 1.014 ms | 1002.704 ms | 17.4 MiB | `cf1a027cb58f80e1` |
| software | text_cached | 18.203 ms | 18.323 ms | 54.9 | 36.359 ms | 2.332 ms | 15.869 ms | 48.8 MiB | `fca4903ca89ab7ef` |
| software | text_churn | 21.310 ms | 22.856 ms | 46.9 | 43.170 ms | 4.653 ms | 16.666 ms | 48.8 MiB | `a6f33a3c1787dcd5` |
| software | frosted_glass | 251.560 ms | 258.252 ms | 4.0 | 255.596 ms | 171.308 ms | 80.177 ms | 22.6 MiB | `006ff481ce2cc672` |
| software | inner_shadow | 65.648 ms | 70.394 ms | 15.2 | 66.600 ms | 34.054 ms | 31.320 ms | 15.3 MiB | `3f12faa6be16ad73` |
| vulkan | solid_rects | 2.267 ms | 3.259 ms | 441.2 | 17.692 ms | 1.296 ms | 0.949 ms | 66.7 MiB | `532a1d781a27158d` |
| vulkan | rounded_ui | 5.094 ms | 5.682 ms | 196.3 | 36.126 ms | 2.984 ms | 2.091 ms | 72.5 MiB | `68a6c6c9346d8a67` |
| vulkan | path_cached | 5.747 ms | 6.460 ms | 174.0 | 21.704 ms | 2.999 ms | 2.722 ms | 67.8 MiB | `2de4f8a1e3b85026` |
| vulkan | path_churn | 6.199 ms | 7.126 ms | 161.3 | 21.727 ms | 3.666 ms | 2.543 ms | 68.4 MiB | `2de4f8a1e3b85026` |
| vulkan | image_grid | 47.344 ms | 52.004 ms | 21.1 | 81.218 ms | 0.620 ms | 46.726 ms | 74.0 MiB | `0a2cdb83ef3d7965` |
| vulkan | clip_layers | 23.150 ms | 24.919 ms | 43.2 | 56.959 ms | 19.148 ms | 3.980 ms | 72.7 MiB | `4fdb0ce07d4e46c7` |
| vulkan | shadow_grid | 1129.557 ms | 1207.655 ms | 0.9 | 1173.318 ms | 0.882 ms | 1128.671 ms | 92.5 MiB | `309cbff8a0daa2cc` |
| vulkan | text_cached | 6.911 ms | 7.401 ms | 144.7 | 83.248 ms | 2.317 ms | 4.614 ms | 93.5 MiB | `119f40f0cbe5c696` |
| vulkan | text_churn | 9.752 ms | 12.197 ms | 102.5 | 95.570 ms | 4.497 ms | 5.199 ms | 96.2 MiB | `ecf9629486bf70e0` |
| vulkan | frosted_glass | 14.667 ms | 15.959 ms | 68.2 | 112.001 ms | 12.583 ms | 1.965 ms | 76.8 MiB | `9b88e962a1300018` |
| vulkan | inner_shadow | 18.625 ms | 19.812 ms | 53.7 | 112.577 ms | 18.148 ms | 0.446 ms | 76.1 MiB | `55ca31b3664d15ed` |

Frame timing is synchronized; readback and hashing are excluded. Pixel hashes validate deterministic output but do not score visual quality.
