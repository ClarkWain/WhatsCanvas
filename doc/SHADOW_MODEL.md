# WhatsCanvas Shadow Model

This document defines the current shadow contract for `Paint::setShadowLayer` and the intended direction for future shadow work.

## Public Paint Contract

`Paint::setShadowLayer(radius, dx, dy, color)` attaches a paint-level shadow to path-based drawing.

- `radius` is a softening radius. Values below zero are treated as zero by the renderer path that consumes the value.
- `dx` and `dy` offset the shadow in the same local coordinate space as the drawing operation.
- `color` carries the shadow color and alpha.
- `Paint::setAlpha` is multiplied into the shadow color.
- A fully transparent shadow color means `Paint::hasShadowLayer()` returns false.
- `Paint::clearShadowLayer()` disables the shadow.

## Shape And Path Shadows

The current implementation applies shadows to path-based drawing:

- Rects, round rects, circles, ovals, arcs, polygons, and custom paths all flow through path rendering.
- Fill shadows are submitted before the normal fill.
- Stroke shadows are submitted before the normal stroke.
- Fill-and-stroke paints can produce both fill and stroke shadows.
- Corner path effects are applied before shadow geometry is generated.
- Dash path effects are applied to the stroke shadow through the same dashed-stroke path as normal strokes.
- The current matrix is applied before the shadow offset, so the offset follows the drawing transform.
- Current rectangular clips and clip masks are carried into the shadow pass.

## Blur Approximation

The current blur is a lightweight multi-pass approximation:

- `radius <= epsilon` emits one offset shadow pass.
- Positive radius emits one central pass plus eight lower-alpha ring samples.
- The ring radius is currently `radius * 0.45`.
- This does not allocate an offscreen blur target and does not run a Gaussian blur shader.
- Bounds calculations conservatively expand by `radius` plus the configured offset.

This model favors simple portability and predictable command submission over high-end soft-shadow quality.

## Text Shadows

Text shadow is not a separate text-specific API today.

- Paint-level shadow is not currently applied inside the text rendering path.
- Future text shadow should be atlas-aware or routed through a general offscreen effect pass.
- CPU text bitmap shadowing should remain optional because it can diverge from GPU text rendering.

## Box Shadow Or Box Gradient

There is no dedicated box-shadow or box-gradient primitive yet.

The preferred future shape is a backend-neutral primitive that can render rounded-rect shadows without tessellating repeated offset copies. It should support:

- rounded rectangle bounds
- per-corner radius
- spread
- blur radius
- offset
- color
- clip behavior matching normal draw calls

## Regression Coverage

Current coverage:

- `PaintStateTests` validates shadow state and transparent-color behavior.
- The validation scene suite includes a gradient/effect scene using paint-level shadows.
- `quickReject(path, paint)` expands path bounds for shadow offset and radius.

Future coverage should add fuzzy visual comparison for shadow scenes because exact pixel hashes can be driver-sensitive.
