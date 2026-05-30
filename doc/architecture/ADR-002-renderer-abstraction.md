# ADR-002: Renderer Abstraction Boundary

## Status

Accepted.

## Context

Canvas previously stored a concrete `Renderer` instance directly. That made the canvas core depend on one specific rendering implementation and prevented gradual backend substitution.

The immediate problem was not just future Metal or Vulkan support. It also made testing, dependency injection, and command recording harder because Canvas owned the implementation instead of an abstraction.

## Decision

Canvas will depend on an interface boundary first, then on a fuller device/backend abstraction later.

The first concrete step is already in place:

- `IRenderer` defines the command queue surface used by `Canvas`;
- `IRenderDevice` now defines the backend/resource surface used by `Renderer`;
- `IRenderTarget` now defines the backend-owned offscreen render target surface used inside the current device implementation;
- non-rect `clipPath` execution now also uses backend-owned clip-resource objects at the render boundary, instead of carrying raw clip triangulation payloads all the way through generic draw execution;
- `Renderer` implements `IRenderer`;
- `OpenGLRenderDevice` implements `IRenderDevice`;
- `Canvas` now owns `std::unique_ptr<IRenderer>`, lazily initializes the active renderer instance, exposes instance-level `shutdown()` for explicit teardown before context destruction, and defaults to the current OpenGL renderer;
- pixel readback, temporary bitmap texture upload, and offscreen layer rendering now route through the renderer/device boundary instead of being managed directly inside `Canvas.cpp`;
- `Image` is now a move-only opaque resource owner, and image-file texture upload routes through the active `IRenderer` instance instead of direct OpenGL calls inside `Image.cpp`;
- shared draw/image data now uses backend-neutral `ImageResource` interfaces backed by renderer-private implementations instead of texture-specific command fields and manual handle/releaser bookkeeping, and temporary resource cleanup no longer happens through direct GL calls in generic draw command destructors;
- blend/scissor/clip state application moved out of generic draw command helper functions into `RenderContext`, OpenGL texture lifecycle helpers are now centralized in `src/opengl/GLTextureUtils.*`, and offscreen render targets now provision stencil attachments for path-based clipping.
- The first `clipPath` slice is now in place: rectangular path clips degenerate to scissor when possible, non-rect path clips precompute a mask at record time, repeated identical clip snapshots can reuse stencil state across adjacent command execution, stacked path clips accumulate through stencil counting, and clip stacks that exceed the current 8-bit stencil budget now fail closed instead of silently wrapping.

This is an intermediate design, not the final backend model.

The target direction is:

- Canvas / recording layer
- renderer interface layer
- render device / resource interfaces
- backend implementation

## Why This Step First

- It removes one direct implementation dependency without rewriting the whole engine.
- It is small enough to validate with existing builds.
- It creates a seam for headless or test renderer implementations later.

## Consequences

### Positive

- Canvas is no longer forced to embed a concrete renderer implementation.
- Constructor injection is now available for future fake or alternate renderers.
- Backend/resource implementation now also has a dedicated seam through `IRenderDevice`.
- Offscreen layer targets now also have a backend-owned object boundary instead of ad-hoc framebuffer lifetime management inside one renderer function.
- Non-rect clip masks now also have a backend-owned resource seam and can be cached on clip state instead of being rebuilt from copied raw arrays on every draw snapshot.
- The remaining Canvas core is closer to a recording/composition layer than a GL state manager.
- Repeated texture lifecycle code and repeated blend/scissor state setup now have single ownership points.

### Negative

- The current split is `Canvas -> IRenderer -> IRenderDevice`, with an initial `IRenderTarget` resource seam, but it is still not yet a full render graph or broader device/resource family.
- `ImageResource` is now an interface boundary, but it still has only one concrete OpenGL implementation and is not yet a fuller device-owned resource family.
- Backend resources are still implemented as OpenGL textures inside the current OpenGL device layer.
- Path clip execution still ends as OpenGL stencil work during command execution, but it now enters that stage through backend-owned clip resources rather than raw generic draw payloads.

## Follow-up

1. Add backend-neutral graphics state and draw model types.
2. Grow the first device/resource abstraction (`IRenderDevice`, `IRenderTarget`, clip resources) into fuller backend-owned resource families (`IImageResource`, reusable clip atlases/masks, ...).
3. Move OpenGL-only state application out of generic command types.
4. Grow the current `ImageResource` interface into a fuller backend-owned image/device resource path.
5. Move clip-mask execution and the remaining OpenGL-only image/path state into a backend resource/device layer.