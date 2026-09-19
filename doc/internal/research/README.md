# Research and Interactive Prototypes

These materials support engineering exploration but are not maintained as part
of the public product documentation.

- `anti-aliasing/` — standalone anti-aliasing tutorial prototype.
- `blend-modes/` — standalone tutorial prototype covering premultiplied
  alpha, all 12 Porter-Duff modes, the three separable modes
  (MULTIPLY / SCREEN / ADD), how each maps to `glBlendFuncSeparate`,
  and cross-library comparisons.
- `command-recording/` — standalone command recording (Picture / DisplayList)
  tutorial prototype covering immediate vs retained rendering, `recordPicture`
  / `drawPicture` / `drawPictureRasterized`, and cross-library comparisons
  (Skia SkPicture, Cairo recording surface, Chrome DisplayList, Flutter
  Picture).
- `offscreen-rendering/` — standalone off-screen rendering tutorial prototype
  covering `OutputTarget`, Software vs OpenGL backend stacks, UI caching, and
  cross-library comparisons.
- `rasterization-and-caching/` — standalone tutorial prototype covering the
  edge-function / barycentric CPU rasterizer, analytic coverage, and how the
  tessellation / glyph-atlas / render-target / picture caches trade compute
  cost for memory footprint.
- `polyline2d/` — standalone polyline tessellation tutorial prototype.
- `font-rendering-techniques/` — background material on font formats, shaping,
  rasterization, GPU text, and the WhatsCanvas text stack.

Promote a prototype into `doc/public/labs/` only when it has a named maintainer,
is linked from the MkDocs navigation, and is validated against the current API.
Otherwise keep it here as research evidence or move it to `doc/archive/` when
it no longer informs active engineering decisions.
