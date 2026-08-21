# Visual validation harnesses

These programs generate focused visual evidence and regression captures. They
are tests rather than end-user examples; real platform behavior is demonstrated
by the hosts under `platforms/`.

- `showcase/` retains the legacy geometry, text and deterministic scene modes
  used by the smoke and pixel-regression scripts.
- `aa_showcase/` isolates analytic anti-aliasing and writes comparison images.
- `image_filter_showcase/` renders the filter and frosted-glass reference UI.

The executable target names are intentionally unchanged so existing validation
commands and baseline workflows remain compatible after the directory move.
