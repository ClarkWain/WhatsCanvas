# Cross-Library 2D Benchmark

Reference renderer: `whatscanvas`. Each candidate uses 2 independent ABBA blocks (4 fresh processes per renderer).

| Adapter | Scene | Process median (95% CI) | Relative (95% CI) | MAE | RMSE | Changed pixels | Quality |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| whatscanvas | `image_grid` | 0.732 ms [0.712, 0.755] | 1.000x [1.000, 1.000] | 0.000 | 0.000 | 0.000% | PASS |
| nanovg | `image_grid` | 0.949 ms [0.945, 0.996] | 1.309x [1.291, 1.328] | 1.524 / 0.030x | 6.307 / 0.096x | 4.755% | PASS |

Relative is the candidate/reference geometric-mean ratio inside each ABBA block; lower is faster. Confidence intervals use a deterministic bootstrap over fresh-process samples. Every JSONL run retains all measured frame samples.

For parameterized scenes with a declared reference background, MAE and RMSE also show error/reference-signal ratios. A blank renderer has a ratio of 1.0 and zero candidate signal, so it fails the combined gate.

A quality failure invalidates the timing comparison.
