# WhatsCanvas Effect Regression Matrix

This matrix records the current regression coverage for paint effects and gradient-heavy rendering.

## Coverage Table

| Effect area | Primary coverage | Secondary coverage | Notes |
| --- | --- | --- | --- |
| Gradients | `scripts/validation_scene_smoke.*` with `gradient-effect` | `PaintStateTests` validates gradient stop normalization | Exact hash output is required by the smoke gate; fuzzy PPM comparison is recommended for portable baselines. |
| Shadows | `gradient-effect` validation scene | `PaintStateTests` validates shadow state and transparent-color behavior | Shadow blur is a lightweight multi-pass approximation; see `doc/SHADOW_MODEL.md`. |
| Blend modes | showcase validation scenes using `ADD`, `SCREEN`, `SRC_IN`, and `DST_OUT` | `doc/BLEND_MODE_AUDIT.md` records GL state mappings | Blend-heavy scenes can be compared with `scripts/compare_ppm_fuzzy.py`. |
| Strokes | showcase geometry scene and matrix/clip tests | `PaintStateTests` validates cap/join state; `measureStrokeBounds` is exercised by the showcase | Stroke mesh generation uses Polyline2D for cap/join output. |
| Dashes | showcase dashed open and closed paths | `PaintStateTests` validates interval normalization and phase handling | Closed seam merge is covered visually in the showcase path section. |

## Recommended Local Commands

```bat
scripts\validation_scene_smoke.bat
ctest -C Debug -R "WhatsCanvas(PaintState|MatrixClip)" --output-on-failure
```

For fuzzy comparison of captured scenes:

```bat
python scripts\compare_ppm_fuzzy.py baseline.ppm candidate.ppm --max-channel-delta 3 --max-mean-delta 0.75 --max-changed-percent 5
```

## Gaps

- Fragment-shader gradient execution is still tracked separately.
- Dedicated box-shadow or box-gradient primitive coverage will be added with that primitive.
- Miter-limit-specific stroke coverage is blocked on a public miter limit or lower-level stroke mesh parameter.
