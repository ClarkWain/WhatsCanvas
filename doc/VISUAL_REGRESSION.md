# WhatsCanvas Visual Regression Notes

WhatsCanvas supports two complementary visual-regression signals:

- exact pixel hashes for deterministic local gates
- fuzzy PPM comparison for driver-sensitive scenes

## Exact Hash Gate

Exact hashes are fast and strict. They are best for local fixed-time, non-MSAA runs where the GPU and driver are stable.

Common environment:

```sh
WHATSCANVAS_EXIT_AFTER_FIRST_FRAME=1
WHATSCANVAS_FIXED_TIME_SECONDS=1.25
WHATSCANVAS_DISABLE_MSAA=1
WHATSCANVAS_PRINT_PIXEL_HASH=1
```

## Fuzzy PPM Comparison

Use `scripts/compare_ppm_fuzzy.py` when exact hashes are too sensitive but the rendered image should still remain visually close to a baseline.

```sh
python scripts/compare_ppm_fuzzy.py baseline.ppm candidate.ppm \
  --max-channel-delta 3 \
  --max-mean-delta 0.75 \
  --max-changed-percent 5.0
```

The script prints machine-readable metrics:

- `FUZZY_PPM_COMPARE_MAX_CHANNEL_DELTA`
- `FUZZY_PPM_COMPARE_MEAN_DELTA`
- `FUZZY_PPM_COMPARE_CHANGED_PERCENT`
- `FUZZY_PPM_COMPARE_RESULT`

The script currently supports binary `P6` PPM files with `maxval=255`, matching `Canvas::savePixelsPPM`.

## Capturing A Validation Scene

```sh
WHATSCANVAS_VALIDATION_SCENE=gradient-effect \
WHATSCANVAS_EXIT_AFTER_FIRST_FRAME=1 \
WHATSCANVAS_FIXED_TIME_SECONDS=1.25 \
WHATSCANVAS_DISABLE_MSAA=1 \
WHATSCANVAS_CAPTURE_PPM=build/gradient-effect.ppm \
./build/WhatsCanvasDemo
```

On Windows PowerShell:

```powershell
$env:WHATSCANVAS_VALIDATION_SCENE = "gradient-effect"
$env:WHATSCANVAS_EXIT_AFTER_FIRST_FRAME = "1"
$env:WHATSCANVAS_FIXED_TIME_SECONDS = "1.25"
$env:WHATSCANVAS_DISABLE_MSAA = "1"
$env:WHATSCANVAS_CAPTURE_PPM = "build\\gradient-effect.ppm"
build\\Debug\\WhatsCanvasDemo.exe
```

## Suggested Scene Policy

- Use exact hashes for stable non-MSAA smoke gates.
- Use fuzzy comparison for shadows, gradients, text, and other driver-sensitive scenes.
- Store baselines outside generated build folders.
- Review fuzzy threshold changes like code changes because loose thresholds can hide real regressions.
