# Cross-platform visual parity

WhatsCanvas treats visual parity as a tested contract rather than a manual
screenshot review. Android, iOS and Desktop must render the same scene content,
orientation, animation time and logical content viewport before their pixels
are compared.

## Canonical content window

Physical device screens cannot have one size. The comparable unit is therefore
the scene's logical content window:

- Landscape: 786 x 377.
- Portrait: 393 x 759.

Every host removes platform safe areas, aspect-fits this canonical canvas,
centers it horizontally and anchors it to the usable top. The complete scene is
scaled once. It never independently reflows cards, text, strokes or animation
geometry. Extra host area is letterboxed with the scene background.

The shared implementation is
[`platforms/shared/scenes/CanonicalViewport.h`](../platforms/shared/scenes/CanonicalViewport.h).
Any new host must use it rather than defining another viewport policy.

## Scene contract

[`tests/visual_parity/scenes.json`](../tests/visual_parity/scenes.json) is the
validation registry. Every scene declares:

- a stable scene id and contract version;
- Android, iOS and Desktop as required platforms;
- portrait and landscape canonical sizes;
- deterministic animation samples in seconds;
- named comparison regions and their tolerance profile.

The current `feature_showcase` samples 0.0, 0.5, 1.25 and 2.0 seconds. New
animation logic must be a pure function of the supplied elapsed time. Random,
wall-clock and locale-dependent input must be seeded or supplied by the scene
contract.

Profiles are intentionally separated:

- `graphics` catches path, clip, gradient, blend and transform drift;
- `image_sampling` keeps a tight mean-error limit while allowing the
  antialiased edge colors produced when a nearest-neighbor texture is captured
  at different physical pixel densities;
- `text` allows limited native rasterization differences while still catching
  missing glyphs, wrapping and geometry changes;
- `layout` catches whole-card displacement and scaling regressions.

The comparator searches a small neighboring-pixel radius before measuring a
delta. This tolerates subpixel edge placement, but not a larger geometry shift.
Threshold changes require a reason and evidence; they must not be raised merely
to make a failing capture pass.

Reference captures must use the canonical logical size with `DPR=3`, then be
normalized by the comparator. Merely rendering a `1179 x 2277` DPR=1 canvas is
not equivalent: text, image sampling, shadows and raster caches still take the
1x path. For example, the portrait Software reference is generated with
`--w=393 --h=759 --dpr=3`.

## Capture identity and metadata

The capture tree consumed by the matrix command is:

```text
captures/<platform>/<scene>/<viewport>/<sample>.png
captures/<platform>/<scene>/<viewport>/<sample>.json   # when cropping is needed
```

PNG, binary PPM and PAM are supported without external Python dependencies.
Device screenshots that include system UI or letterboxing must provide a JSON
sidecar:

```json
{
  "schema_version": 1,
  "scene_id": "feature_showcase",
  "viewport_id": "portrait",
  "sample_id": "t1250",
  "platform": "ios",
  "backend": "metal",
  "content_rect_pixels": [0, 186, 1206, 2329]
}
```

`content_rect_pixels` is the exact canonical scene rectangle after safe-area
layout and before normalization. The comparator rejects an aspect mismatch
instead of guessing a crop.

Desktop accepts `--time=<seconds>` for deterministic dumps and `--dpr=3` to
match high-density mobile rasterization. Android accepts the
`capture_time_seconds` Activity extra. iOS accepts
`--capture-time=<seconds>` together with its opt-in `--capture-frames` switch.

## Commands

Validate the registry and comparator:

```sh
python3 tools/visual_parity/visual_parity.py validate \
  --contract tests/visual_parity/scenes.json
python3 tests/VisualParityToolTests.py
```

Compare one pair and write machine-readable results plus a heat map:

```sh
python3 tools/visual_parity/visual_parity.py compare \
  --contract tests/visual_parity/scenes.json \
  --scene feature_showcase --viewport landscape --sample t1250 \
  --reference captures/desktop/feature_showcase/landscape/t1250.ppm \
  --actual captures/android/feature_showcase/landscape/t1250.png \
  --actual-metadata captures/android/feature_showcase/landscape/t1250.json \
  --report out/visual-parity/android-landscape.json \
  --diff out/visual-parity/android-landscape.pam
```

Run the complete required-platform matrix:

```sh
python3 tools/visual_parity/visual_parity.py matrix \
  --contract tests/visual_parity/scenes.json \
  --captures captures --reference-platform desktop \
  --output out/visual-parity
```

## Validation layers

1. Contract and comparator unit tests run on every change.
2. Deterministic Software golden tests guard the reference rasterizer.
3. OpenGL, OpenGL ES and Metal tests compare their backend against Software.
4. The device matrix compares normalized Android, iOS and Desktop captures.
5. Lifecycle, rotation, cold start and background/foreground tests run outside
   the pixel comparator, then capture the same deterministic scene again.

Layers 1-3 are fast pull-request gates. The full device matrix should run on
rendering pull requests, nightly, and before release. Missing required captures
are failures, not skips, in the release job.

## Adding a scene

1. Put platform-independent drawing code under `platforms/shared/scenes/` and
   use only the public `wsc::Canvas` API.
2. Register the same stable id in every platform host.
3. Add both canonical orientations and meaningful animation samples to
   `scenes.json`.
4. Define focused regions for every feature the scene is intended to cover.
5. Generate a Software golden and backend parity test.
6. Capture all required platforms and run the matrix.
7. Document any intentional platform-specific region. Do not silently mask it.

Platform branding can remain outside the strict graphics regions. Rendering
content, geometry, timing and resource sampling cannot be platform-specific.
