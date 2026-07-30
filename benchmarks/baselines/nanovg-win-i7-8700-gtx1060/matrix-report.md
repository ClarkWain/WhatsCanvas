# Cross-Library Parameter Matrix

Preset: `standard`. Reference: `whatscanvas`. Each matrix cell uses 2 ABBA blocks and 4 fresh processes per renderer.

| Scene | Mode | Ops | Seed | Texture / rounded / state / text | whatscanvas median (95% CI) | Candidate median (95% CI) | Candidate / reference (95% CI) | Quality | Verdict |
| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | --- | --- |
| `geometry_stress` | `stable` | 256 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.626 ms [0.535, 0.663] | 0.826 ms [0.811, 0.878] | 1.368x [1.304, 1.433] | PASS | whatscanvas faster |
| `geometry_stress` | `dynamic-data` | 256 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.652 ms [0.592, 0.765] | 0.800 ms [0.728, 0.846] | 1.201x [1.105, 1.297] | PASS | whatscanvas faster |
| `geometry_stress` | `dynamic-structure` | 256 | 1001 | 1 / 0.000 / 0.125 / 24 | 0.788 ms [0.743, 0.832] | 0.811 ms [0.765, 0.858] | 1.033x [0.954, 1.113] | PASS | inconclusive |
| `geometry_stress` | `stable` | 1024 | 1001 | 1 / 0.000 / 0.000 / 24 | 1.092 ms [1.076, 1.202] | 1.604 ms [1.590, 1.615] | 1.439x [1.411, 1.468] | PASS | whatscanvas faster |
| `geometry_stress` | `dynamic-data` | 1024 | 1001 | 1 / 0.000 / 0.000 / 24 | 1.126 ms [1.097, 1.299] | 1.607 ms [1.575, 1.912] | 1.444x [1.346, 1.542] | PASS | whatscanvas faster |
| `geometry_stress` | `dynamic-structure` | 1024 | 1001 | 1 / 0.000 / 0.125 / 24 | 1.730 ms [1.693, 1.970] | 1.906 ms [1.796, 2.153] | 1.089x [1.083, 1.095] | PASS | whatscanvas faster |
| `geometry_stress` | `stable` | 4096 | 1001 | 1 / 0.000 / 0.000 / 24 | 3.971 ms [3.925, 4.312] | 5.417 ms [5.003, 5.839] | 1.341x [1.259, 1.423] | PASS | whatscanvas faster |
| `geometry_stress` | `dynamic-data` | 4096 | 1001 | 1 / 0.000 / 0.000 / 24 | 3.893 ms [3.777, 3.932] | 5.175 ms [4.975, 5.457] | 1.340x [1.328, 1.353] | PASS | whatscanvas faster |
| `geometry_stress` | `dynamic-structure` | 4096 | 1001 | 1 / 0.000 / 0.125 / 24 | 5.751 ms [5.712, 5.978] | 6.029 ms [5.812, 6.170] | 1.037x [1.030, 1.043] | PASS | whatscanvas faster |
| `image_grid` | `stable` | 64 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.203 ms [0.200, 0.265] | 0.312 ms [0.305, 0.326] | 1.454x [1.376, 1.532] | PASS | whatscanvas faster |
| `image_grid` | `dynamic-data` | 64 | 1001 | 4 / 0.250 / 0.000 / 24 | 0.218 ms [0.200, 0.270] | 0.340 ms [0.308, 0.358] | 1.505x [1.317, 1.693] | PASS | whatscanvas faster |
| `image_grid` | `dynamic-structure` | 64 | 1001 | 32 / 0.500 / 0.125 / 24 | 0.354 ms [0.315, 0.371] | 0.422 ms [0.401, 0.455] | 1.225x [1.107, 1.342] | PASS | whatscanvas faster |
| `image_grid` | `stable` | 256 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.333 ms [0.311, 0.371] | 0.473 ms [0.460, 0.490] | 1.410x [1.398, 1.421] | PASS | whatscanvas faster |
| `image_grid` | `dynamic-data` | 256 | 1001 | 4 / 0.250 / 0.000 / 24 | 0.374 ms [0.359, 0.400] | 0.605 ms [0.580, 0.672] | 1.632x [1.619, 1.646] | PASS | whatscanvas faster |
| `image_grid` | `dynamic-structure` | 256 | 1001 | 32 / 0.500 / 0.125 / 24 | 0.664 ms [0.636, 0.666] | 0.874 ms [0.794, 0.904] | 1.310x [1.248, 1.372] | PASS | whatscanvas faster |
| `image_grid` | `stable` | 1024 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.732 ms [0.712, 0.755] | 0.949 ms [0.945, 0.996] | 1.309x [1.291, 1.328] | PASS | whatscanvas faster |
| `image_grid` | `dynamic-data` | 1024 | 1001 | 4 / 0.250 / 0.000 / 24 | 0.737 ms [0.730, 1.213] | 1.778 ms [1.708, 1.824] | 2.152x [1.823, 2.482] | PASS | whatscanvas faster |
| `image_grid` | `dynamic-structure` | 1024 | 1001 | 32 / 0.500 / 0.125 / 24 | 1.328 ms [1.302, 1.374] | 1.869 ms [1.864, 1.919] | 1.411x [1.410, 1.412] | PASS | whatscanvas faster |
| `contract_text_latin` | `stable` | 64 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.476 ms [0.466, 0.493] | 0.573 ms [0.561, 0.587] | 1.200x [1.184, 1.215] | PASS | whatscanvas faster |
| `contract_text_latin` | `dynamic-data` | 64 | 1001 | 1 / 0.000 / 0.000 / 24 | 0.505 ms [0.467, 0.555] | 0.584 ms [0.538, 0.648] | 1.159x [1.115, 1.203] | PASS | whatscanvas faster |
| `contract_text_latin` | `dynamic-structure` | 64 | 1001 | 1 / 0.000 / 0.062 / 32 | 0.660 ms [0.620, 0.683] | 0.808 ms [0.759, 0.869] | 1.236x [1.194, 1.278] | PASS | whatscanvas faster |
| `contract_text_latin` | `stable` | 256 | 1001 | 1 / 0.000 / 0.000 / 24 | 1.464 ms [1.314, 1.491] | 1.995 ms [1.860, 2.126] | 1.392x [1.346, 1.437] | PASS | whatscanvas faster |
| `contract_text_latin` | `dynamic-data` | 256 | 1001 | 1 / 0.000 / 0.000 / 24 | 1.747 ms [1.521, 1.817] | 1.944 ms [1.928, 1.961] | 1.141x [1.113, 1.169] | PASS | whatscanvas faster |
| `contract_text_latin` | `dynamic-structure` | 256 | 1001 | 1 / 0.000 / 0.062 / 32 | 2.094 ms [1.923, 2.336] | 2.637 ms [2.461, 2.774] | 1.246x [1.201, 1.291] | PASS | whatscanvas faster |
| `contract_text_latin` | `stable` | 1024 | 1001 | 1 / 0.000 / 0.000 / 24 | 6.556 ms [6.361, 6.833] | 6.953 ms [6.788, 7.265] | 1.063x [1.047, 1.079] | PASS | whatscanvas faster |
| `contract_text_latin` | `dynamic-data` | 1024 | 1001 | 1 / 0.000 / 0.000 / 24 | 6.890 ms [6.790, 7.056] | 7.104 ms [6.965, 7.159] | 1.026x [1.015, 1.036] | PASS | whatscanvas faster |
| `contract_text_latin` | `dynamic-structure` | 1024 | 1001 | 1 / 0.000 / 0.062 / 32 | 6.886 ms [6.843, 7.033] | 10.132 ms [10.074, 10.239] | 1.468x [1.454, 1.481] | PASS | whatscanvas faster |

## Summary

- Quality gates passed: 27/27.
- Reference faster with the full 95% CI above 1.0: 26.
- Candidate faster with the full 95% CI below 1.0: 0.
- Inconclusive at this repetition count: 1.

The ratio is candidate/reference inside each ABBA block, so values above 1.0 favor the reference. A quality failure invalidates timing. Each cell retains its raw frame arrays, captures, process order, and quality metrics in its own directory.
