# Cross-Library 2D Benchmark

Reference renderer: `whatscanvas`. Each candidate uses 2 independent ABBA blocks (4 fresh processes per renderer).

| Adapter | Scene | Process median (95% CI) | Relative (95% CI) | MAE | RMSE | Changed pixels | Quality |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| whatscanvas | `image_grid` | 2.177 ms [2.153, 2.208] | 1.000x [1.000, 1.000] | 0.000 | 0.000 | 0.000% | PASS |
| nanovg | `image_grid` | 0.810 ms [0.748, 0.814] | 0.365x [0.356, 0.374] | 2.181 / 0.045x | 8.594 / 0.133x | 5.374% | PASS |

Relative is the candidate/reference geometric-mean ratio inside each ABBA block; lower is faster. Confidence intervals use a deterministic bootstrap over fresh-process samples. Every JSONL run retains all measured frame samples.

For parameterized scenes with a declared reference background, MAE and RMSE also show error/reference-signal ratios. A blank renderer has a ratio of 1.0 and zero candidate signal, so it fails the combined gate.

A quality failure invalidates the timing comparison.
