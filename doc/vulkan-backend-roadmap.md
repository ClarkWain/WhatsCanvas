# Vulkan Backend Roadmap

Status: **experimental backend in hardening**

The Vulkan backend goal is to make `RenderBackendType::Vulkan` a first-class
selectable backend whose Canvas output matches the OpenGL path on representative
scenes. It is not the default backend yet; OpenGL remains the production path.

## Completed Foundation

| Milestone | Status |
| --- | --- |
| M1 render core | Done: instance/device/queue selection, command pool, single-time submit helpers |
| M2 render target + readback | Done: offscreen image, render pass, framebuffer, staging readback |
| M3 geometry | Done for stable command translation: path triangles, points, lines |
| M4 paint | Done for solid color, per-vertex color, shader gradients, and blend modes covered by tests |
| M5 textures | Done for sampled images, uploads, partial updates, tint, color matrix, and non-owning external sampled images |
| M6 layer mechanism | Done for native layer composite and command-stream snapshot to sampled texture |
| M7 clip | Done for coverage-mask mechanism and command scissor parity with OpenGL |
| M8 present seed | Done as standalone one-frame GLFW swapchain present example |
| ADR-006 command translation | In progress but useful: Vulkan reads the same command payloads and translates them into backend primitives |
| Visual parity smoke | Done for a stable OpenGL/Vulkan command scene covering solid, blend, gradient, scissor, and text geometry |

## Current Definition Of Done For Experimental Vulkan

The current branch is considered experimental-ready when:

- `ctest -L vulkan` passes on a Vulkan-capable machine.
- The default Vulkan-off build remains green.
- `WhatsCanvasBackendVisualParityTests` passes.
- Vulkan resources are non-owning/owning where documented and do not leak across
  finalize paths.
- Unsupported semantics fail explicitly rather than silently rendering partial
  output.

## Remaining Parity Work

### P0: Broaden Visual Parity

Add OpenGL/Vulkan parity cases for:

- texture sampling, tint, color matrix, and tile modes;
- saveLayer and layer alpha;
- radial gradients and more gradient tile modes;
- coverage-mask clip scenes;
- larger validation-scene subsets.

### P1: Canvas Swapchain Path

The current `examples/vulkan_present` proves real Vulkan windowed presentation,
but it presents a cleared frame from a standalone swapchain. The next step is a
Canvas-backed Vulkan present path:

- create a surface-aware Vulkan context;
- render Canvas command output into a swapchain image or intermediate target;
- handle acquire/submit/present synchronization;
- recreate swapchain on resize.

### P2: Backend-Neutral Command Layer

The current Vulkan translator reads existing command payloads pragmatically.
Long term, OpenGL and Vulkan should consume a shared backend-neutral primitive
stream:

- extract command-to-primitive conversion away from GL execution;
- keep OpenGL behavior covered by existing pixel/hash tests;
- point Vulkan at the same primitives;
- make future Metal/WebGPU/software backends cheaper to add.

### P3: Text Rendering Parity

Vulkan currently covers text as vector geometry. Full text parity should include:

- glyph atlas texture sampling path;
- atlas dirty-rect upload semantics;
- text shadows and layer interactions;
- text pixel parity cases where driver differences are manageable.

### P4: CI And Release Surfacing

- Keep Vulkan tests optional unless CI has a reliable Vulkan-capable runtime.
- Publish clear backend capability tables in README and release notes.
- Mark Vulkan APIs and examples experimental until Canvas-level parity is broad.

## Risks

- Vulkan and OpenGL coordinate systems differ; every new path needs explicit
  origin/parity tests.
- Windowed present is platform and driver dependent, so it should not become a
  brittle default CI gate.
- Duplicating command translation can drift from OpenGL behavior; the
  backend-neutral command layer is the long-term fix.
