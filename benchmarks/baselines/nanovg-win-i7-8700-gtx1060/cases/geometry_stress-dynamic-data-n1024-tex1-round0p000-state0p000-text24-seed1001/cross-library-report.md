# Cross-Library 2D Benchmark

Reference renderer: `whatscanvas`. Each candidate uses 2 independent ABBA blocks (4 fresh processes per renderer).

| Adapter | Scene | Process median (95% CI) | Relative (95% CI) | MAE | RMSE | Changed pixels | Quality |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| whatscanvas | `geometry_stress` | 1.126 ms [1.097, 1.299] | 1.000x [1.000, 1.000] | 0.000 | 0.000 | 0.000% | PASS |
| nanovg | `geometry_stress` | 1.607 ms [1.575, 1.912] | 1.444x [1.346, 1.542] | 0.188 | 0.832 | 0.003% | PASS |

Relative is the candidate/reference geometric-mean ratio inside each ABBA block; lower is faster. Confidence intervals use a deterministic bootstrap over fresh-process samples. Every JSONL run retains all measured frame samples.

For parameterized scenes with a declared reference background, MAE and RMSE also show error/reference-signal ratios. A blank renderer has a ratio of 1.0 and zero candidate signal, so it fails the combined gate.

A quality failure invalidates the timing comparison.
