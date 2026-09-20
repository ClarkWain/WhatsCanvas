# WhatsCanvas 1.2.1

WhatsCanvas 1.2.1 is a maintenance release that fixes an OpenGL rendering
regression involving `clipPath`, ships a Software vs OpenGL diagnostic tool,
and hardens the release-quality validation of the tree. Public API surface
is unchanged; the package layout, target names, and semantic versioning
guarantees continue from 1.2.0.

## Fixed

- OpenGL backend rendered `clipPath` combined with a gradient rect (or any
  batched second path in the same run) as a fully-blank frame while the
  Software backend produced the expected clipped output.
  `RenderContext::applyClipState` now saves and restores `GL_CURRENT_PROGRAM`
  and `GL_VERTEX_ARRAY_BINDING` around the clip-coverage offscreen pass so the
  downstream `DrawPathProgram` batching cache stays consistent with real GL
  state. `DrawPathProgram` also pre-initialises `uClipMask` to a stable
  texture unit, and `ClipCoverageProgram::drawCoverage` mirrors the Software
  fallback that treats a missing per-vertex coverage array as fully opaque.
- Desktop example smoke scripts (`examples/game/tetris`, `spider_solitaire`,
  `racer`) wipe a stale `CMakeCache.txt` whose `CMAKE_HOME_DIRECTORY` points at
  a different repo path, restoring reliable clean-tree reconfiguration on
  developer machines that share a build directory across multiple worktrees.

## Added

- `examples/parity_probe/` — a Software vs OpenGL diagnostic tool that
  renders 54 mini-scenes (rect / circle / rounded rect / stroke variants,
  gradients, shadows, `clipRect` / `clipPath` variants, `saveLayer` and
  filter combinations, all Porter-Duff blend modes, images, paths, arcs)
  through both backends, reports per-scene `max` / `mean` / `bad-pixel-ratio`
  deltas and drops per-scene PPM triples for visual inspection. Registered
  as a CTest diagnostic (`ctest -L parity-probe`); never gates default CI.

## Compatibility

`whatscanvas-<platform>-release-1.2.1` packages are drop-in binary-compatible
replacements for the 1.2.0 packages. `find_package(WhatsCanvas 1.2.0 CONFIG
REQUIRED)` consumers can bump their required version to 1.2.1 without any
source changes. `Paint`, `Canvas`, and `Path` public headers are byte-for-byte
identical apart from the bumped `WSC_VERSION_STRING`.

## Distribution

Release assets use the existing package layout and target names:

- `whatscanvas-win64-release-1.2.1.zip`
- `whatscanvas-linux-x64-release-1.2.1.zip`
- `whatscanvas-macos-universal-release-1.2.1.zip`
- `whatscanvas-android-release-1.2.1.aar` (Prefab SDK)
- `whatscanvas-ios-release-1.2.1.zip` (XCFramework)

See [CHANGELOG](../../../../CHANGELOG.md) for the full change list and
[API stability](../../../public/reference/API_STABILITY.md) for support boundaries.
