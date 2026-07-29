# Cross-Library 2D Benchmark

Reference renderer: `whatscanvas`. Each candidate uses 2 independent ABBA blocks (4 fresh processes per renderer).

| Adapter | Scene | Process median (95% CI) | Relative (95% CI) | MAE | RMSE | Changed pixels | Quality |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| whatscanvas | `contract_text_latin` | 6.736 ms [6.476, 7.455] | 1.000x [1.000, 1.000] | 0.000 | 0.000 | 0.000% | PASS |
| nanovg | `contract_text_latin` | 7.287 ms [6.953, 7.796] | 1.071x [1.025, 1.117] | 17.655 / 0.370x | 43.294 / 0.490x | 20.893% | PASS |

Relative is the candidate/reference geometric-mean ratio inside each ABBA block; lower is faster. Confidence intervals use a deterministic bootstrap over fresh-process samples. Every JSONL run retains all measured frame samples.

For parameterized scenes with a declared reference background, MAE and RMSE also show error/reference-signal ratios. A blank renderer has a ratio of 1.0 and zero candidate signal, so it fails the combined gate.

A quality failure invalidates the timing comparison.
