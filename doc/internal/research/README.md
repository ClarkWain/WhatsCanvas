# Research and Interactive Prototypes

These materials support engineering exploration but are not maintained as part
of the public product documentation.

- `anti-aliasing/` — standalone anti-aliasing tutorial prototype.
- `blend-modes/` — standalone tutorial prototype covering premultiplied
  alpha, all 12 Porter-Duff modes, the three separable modes
  (MULTIPLY / SCREEN / ADD), how each maps to `glBlendFuncSeparate`,
  and cross-library comparisons.
- `canvas-math/` — standalone tutorial prototype covering the math used
  inside 2D graphics libraries: 2D vectors (dot / cross products),
  homogeneous 3×3 / 4×4 matrices, cubic Bezier curves + de Casteljau +
  flatten, linear / radial gradient parameterization, signed distance
  fields (SDF), and barycentric interpolation.
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
- `depth-and-occlusion/` — standalone tutorial prototype covering depth
  buffer basics (1/z mapping, precision), depth test / early-Z / Hi-Z,
  stencil buffer patterns, transparent ordering / OIT concepts, z-fighting
  + polygon offset, and how 2D libraries (WhatsCanvas) handle z-order via
  painter's algorithm.
- `temporal-antialiasing/` — standalone tutorial prototype covering TAA:
  why MSAA misses shading noise, Halton 2/3 jitter, reprojection + motion
  vectors, ghosting, neighborhood clip / clamp in YCoCg, disocclusion
  handling, sharpen pass, and the relationship to DLSS / FSR2 / TSR.
- `post-processing-pipeline/` — standalone tutorial prototype covering the
  full post-FX chain: HDR + linear space, Bloom pyramid, Tonemap
  (Reinhard / ACES / AgX), 3D LUT color grading, vignette / chromatic
  aberration / grain, motion blur + DOF, FXAA / SMAA, and the canonical
  pass ordering.

Promote a prototype into `doc/public/labs/` only when it has a named maintainer,
is linked from the MkDocs navigation, and is validated against the current API.
Otherwise keep it here as research evidence or move it to `doc/archive/` when
it no longer informs active engineering decisions.
