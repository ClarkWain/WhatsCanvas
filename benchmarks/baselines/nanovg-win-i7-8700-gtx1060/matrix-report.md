# Cross-Library Parameter Matrix

Preset: `standard`. Reference: `whatscanvas`. Each matrix cell uses 2 ABBA blocks and 4 fresh processes per renderer.

| Scene | Mode | Ops | Seed | Texture / rounded / state / text | whatscanvas median (95% CI) | Candidate median (95% CI) | Candidate / reference (95% CI) | Quality | Verdict |
| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | --- | --- |
| `geometry_stress` | `stable` | 256 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.563 ms [0.524, 0.625] | 0.865 ms [0.767, 1.085] | 1.576x [1.403, 1.748] | PASS | whatscanvas faster |
| `geometry_stress` | `dynamic-data` | 256 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.759 ms [0.751, 0.777] | 0.748 ms [0.729, 0.798] | 0.992x [0.968, 1.015] | PASS | inconclusive |
| `geometry_stress` | `dynamic-structure` | 256 | 1001 | 1 / 0.000 / 0.125 / 24 | 1.347 ms [1.253, 1.371] | 0.790 ms [0.775, 0.806] | 0.595x [0.586, 0.603] | PASS | nanovg faster |
| `geometry_stress` | `stable` | 1024 | 1001 | 1 / 0.000 / 0.000 / 24 | 1.334 ms [1.320, 1.386] | 1.567 ms [1.511, 1.578] | 1.158x [1.158, 1.159] | PASS | whatscanvas faster |
| `geometry_stress` | `dynamic-data` | 1024 | 1001 | 1 / 0.000 / 0.000 / 24 | 1.952 ms [1.935, 1.998] | 1.551 ms [1.509, 1.558] | 0.787x [0.772, 0.803] | PASS | nanovg faster |
| `geometry_stress` | `dynamic-structure` | 1024 | 1001 | 1 / 0.000 / 0.125 / 24 | 3.111 ms [3.068, 3.207] | 2.022 ms [1.993, 2.048] | 0.647x [0.646, 0.648] | PASS | nanovg faster |
| `geometry_stress` | `stable` | 4096 | 1001 | 1 / 0.000 / 0.000 / 24 | 7.081 ms [6.543, 8.097] | 4.877 ms [4.827, 4.923] | 0.680x [0.650, 0.709] | PASS | nanovg faster |
| `geometry_stress` | `dynamic-data` | 4096 | 1001 | 1 / 0.000 / 0.000 / 24 | 6.688 ms [6.500, 6.952] | 4.813 ms [4.731, 5.305] | 0.733x [0.712, 0.753] | PASS | nanovg faster |
| `geometry_stress` | `dynamic-structure` | 4096 | 1001 | 1 / 0.000 / 0.125 / 24 | 9.819 ms [9.736, 9.995] | 5.751 ms [5.710, 5.869] | 0.586x [0.582, 0.590] | PASS | nanovg faster |
| `image_grid` | `stable` | 64 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.194 ms [0.180, 0.243] | 0.289 ms [0.280, 0.301] | 1.437x [1.428, 1.446] | PASS | whatscanvas faster |
| `image_grid` | `dynamic-data` | 64 | 1001 | 4 / 0.250 / 0.000 / 24 | 0.721 ms [0.708, 0.777] | 0.362 ms [0.354, 0.387] | 0.501x [0.479, 0.523] | PASS | nanovg faster |
| `image_grid` | `dynamic-structure` | 64 | 1001 | 32 / 0.500 / 0.125 / 24 | 0.931 ms [0.921, 0.958] | 0.448 ms [0.396, 0.473] | 0.471x [0.467, 0.475] | PASS | nanovg faster |
| `image_grid` | `stable` | 256 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.366 ms [0.341, 0.376] | 0.463 ms [0.442, 0.480] | 1.276x [1.227, 1.326] | PASS | whatscanvas faster |
| `image_grid` | `dynamic-data` | 256 | 1001 | 4 / 0.250 / 0.000 / 24 | 1.721 ms [1.689, 1.733] | 0.607 ms [0.596, 0.620] | 0.354x [0.353, 0.355] | PASS | nanovg faster |
| `image_grid` | `dynamic-structure` | 256 | 1001 | 32 / 0.500 / 0.125 / 24 | 2.177 ms [2.153, 2.208] | 0.810 ms [0.748, 0.814] | 0.365x [0.356, 0.374] | PASS | nanovg faster |
| `image_grid` | `stable` | 1024 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.733 ms [0.728, 0.740] | 0.933 ms [0.892, 0.951] | 1.264x [1.255, 1.274] | PASS | whatscanvas faster |
| `image_grid` | `dynamic-data` | 1024 | 1001 | 4 / 0.250 / 0.000 / 24 | 4.273 ms [4.233, 4.534] | 1.683 ms [1.666, 1.896] | 0.400x [0.391, 0.408] | PASS | nanovg faster |
| `image_grid` | `dynamic-structure` | 1024 | 1001 | 32 / 0.500 / 0.125 / 24 | 5.762 ms [5.744, 5.786] | 1.776 ms [1.691, 1.856] | 0.308x [0.304, 0.312] | PASS | nanovg faster |
| `contract_text_latin` | `stable` | 64 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.503 ms [0.449, 0.622] | 0.549 ms [0.510, 0.557] | 1.052x [0.971, 1.132] | PASS | inconclusive |
| `contract_text_latin` | `dynamic-data` | 64 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.584 ms [0.565, 0.626] | 0.583 ms [0.578, 0.591] | 0.991x [0.981, 1.001] | PASS | inconclusive |
| `contract_text_latin` | `dynamic-structure` | 64 | 1001 | 1 / 0.000 / 0.062 / 32 | 0.700 ms [0.647, 0.714] | 0.794 ms [0.757, 0.810] | 1.143x [1.140, 1.146] | PASS | whatscanvas faster |
| `contract_text_latin` | `stable` | 256 | 1001 | 1 / 0.000 / 0.000 / 24 | 1.436 ms [1.416, 1.669] | 1.886 ms [1.836, 1.891] | 1.263x [1.200, 1.326] | PASS | whatscanvas faster |
| `contract_text_latin` | `dynamic-data` | 256 | 1001 | 1 / 0.000 / 0.000 / 24 | 1.745 ms [1.663, 1.829] | 1.896 ms [1.854, 1.907] | 1.083x [1.059, 1.107] | PASS | whatscanvas faster |
| `contract_text_latin` | `dynamic-structure` | 256 | 1001 | 1 / 0.000 / 0.062 / 32 | 1.992 ms [1.978, 2.151] | 2.607 ms [2.532, 2.753] | 1.294x [1.288, 1.300] | PASS | whatscanvas faster |
| `contract_text_latin` | `stable` | 1024 | 1001 | 1 / 0.000 / 0.000 / 24 | 6.736 ms [6.476, 7.455] | 7.287 ms [6.953, 7.796] | 1.071x [1.025, 1.117] | PASS | whatscanvas faster |
| `contract_text_latin` | `dynamic-data` | 1024 | 1001 | 1 / 0.000 / 0.000 / 24 | 6.661 ms [6.568, 7.419] | 7.010 ms [6.844, 7.204] | 1.029x [1.007, 1.051] | PASS | whatscanvas faster |
| `contract_text_latin` | `dynamic-structure` | 1024 | 1001 | 1 / 0.000 / 0.062 / 32 | 7.650 ms [6.860, 7.836] | 10.585 ms [10.252, 10.862] | 1.412x [1.369, 1.455] | PASS | whatscanvas faster |

## Summary

- Quality gates passed: 27/27.
- Reference faster with the full 95% CI above 1.0: 12.
- Candidate faster with the full 95% CI below 1.0: 12.
- Inconclusive at this repetition count: 3.

The ratio is candidate/reference inside each ABBA block, so values above 1.0 favor the reference. A quality failure invalidates timing. Each cell retains its raw frame arrays, captures, process order, and quality metrics in its own directory.
