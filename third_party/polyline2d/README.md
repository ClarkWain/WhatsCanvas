# Polyline2D (vendored)

Header-only 2D polyline meshing library by Marius Metzger (CrushedPixel),
upstream: https://github.com/crushedpixel/Polyline2D

This is a **vendored** copy (plain source files, not a git submodule). It is
kept in-tree because WhatsCanvas depends on a local patch that is not present
upstream:

- `Polyline2D::create(...)` accepts an additional `miterLimit` parameter used to
  drive the stroke miter limit exposed via `wsc::Paint::setStrokeMiterLimit`.

Only the headers under `include/` are used, and only internally by the
WhatsCanvas implementation. They are not part of the public `wsc` API and are
not installed with the package.

Licensed under the MIT License; see `LICENSE.md`.
