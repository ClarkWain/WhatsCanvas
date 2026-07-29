# Cross-Library 2D Benchmark

Reference renderer: `whatscanvas`. Each candidate uses 4 independent ABBA blocks (8 fresh processes per renderer).

| Adapter | Scene | Process median (95% CI) | Relative (95% CI) | MAE | RMSE | Changed pixels | Quality |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| whatscanvas | `geometry_stress` | 2.617 ms [2.572, 2.764] | 1.000x [1.000, 1.000] | 0.000 | 0.000 | 0.000% | PASS |
| nanovg | `geometry_stress` | 3.705 ms [3.605, 3.807] | 1.407x [1.273, 1.441] | 0.242 | 1.003 | 0.000% | PASS |
| whatscanvas | `image_grid` | 0.272 ms [0.262, 0.304] | 1.000x [1.000, 1.000] | 0.000 | 0.000 | 0.000% | PASS |
| nanovg | `image_grid` | 0.383 ms [0.380, 0.388] | 1.369x [1.347, 1.417] | 0.275 | 0.600 | 0.019% | PASS |
| whatscanvas | `contract_text_latin` | 2.878 ms [2.670, 3.021] | 1.000x [1.000, 1.000] | 0.000 | 0.000 | 0.000% | PASS |
| nanovg | `contract_text_latin` | 3.292 ms [3.255, 3.337] | 1.153x [1.105, 1.169] | 7.744 | 26.566 | 8.551% | PASS |

Relative is the candidate/reference geometric-mean ratio inside each ABBA block; lower is faster. Confidence intervals use a deterministic bootstrap over fresh-process samples. Every JSONL run retains all measured frame samples.

A quality failure invalidates the timing comparison.
