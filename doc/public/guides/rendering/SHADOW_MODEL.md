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

Text shadow is not a separate text-specific API today. `drawText` and `drawTextBox` consume the same
paint-level shadow layer used by shape drawing.

- Geometry text submits shadow passes before the normal text command.
- Bitmap text submits tinted shadow image passes before the normal tinted text image.
- Atlas text submits dedicated multi-sample shadow image passes tuned for glyph texture quads.
- The current matrix, rectangular clips, clip masks, alpha, and blend mode are preserved for shadow passes.
- A future high-quality implementation can route text shadows through a general offscreen effect pass.

## Box Shadow Or Box Gradient

`Canvas::drawBoxShadow` provides the current shadow-oriented box primitive. It expands or shrinks the rounded rectangle by `spread`, applies `blurRadius`, `dx`, `dy`, and `color`, then routes the draw through the existing paint shadow pipeline.

The current primitive supports:

- rounded rectangle bounds
- per-corner radius
- spread
- blur radius
- offset
- color
- clip behavior matching normal draw calls

A future optimized implementation can render rounded-rect shadows without tessellating repeated offset copies.

## Regression Coverage

Current coverage:

- `PaintStateTests` validates shadow state and transparent-color behavior.
- `ContextLifecycleTests` validates command submission for box shadows and text shadows.
- The validation scene suite includes a gradient/effect scene using paint-level shadows.
- `quickReject(path, paint)` expands path bounds for shadow offset and radius.
- Future coverage should add fuzzy visual comparison for shadow scenes because exact pixel hashes can be driver-sensitive.
