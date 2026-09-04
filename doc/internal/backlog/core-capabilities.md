# Optional Product Expansion Register

Status reviewed: 2026-09-04.

The stable v1 product scope is complete. Canvas drawing, text, desktop and
mobile packaging, representative mobile hardware validation, and complex
application usage are established. The mobile result is consolidated in
[`mobile-hardware-signoff-2026-09.md`](../validation/mobile-hardware-signoff-2026-09.md).

The proposals below do not represent missing v1 capabilities and do not have a
release priority. Promote one to an active issue only after defining its user,
owner, milestone, compatibility boundary, and acceptance evidence.

## Web delivery options

- Publish a prebuilt Web artifact containing `.wasm`, an ES module loader,
  browser requirements, version metadata, and licenses.
- Add a clean-consumer smoke test that downloads and runs the exact Web
  artifact attached to a release.
- Add browser fetch and repaint/relayout glue around the existing remote-font
  provider when a browser-facing product needs it.
- Define a JavaScript/TypeScript Canvas API only if WhatsCanvas chooses to
  support JavaScript consumers directly. The current C++ API and Web host do
  not require that expansion.

## Backend options

- Extend library-owned Vulkan window presentation beyond Win32 if a target
  application needs it. Host-owned presentation and existing platform hosts
  remain valid current integrations.
- Evaluate WebGPU only after a concrete consumer demonstrates an advantage over
  the validated WebGL 2 path that justifies another maintained backend.

## Promotion rules

- Optional proposals do not reduce the completion status of the stable product.
- A proposal becomes active work only through an issue or project milestone;
  this file is not an implicit roadmap commitment.
- Public API expansion requires an accepted boundary and lifetime model before
  implementation.
- Completed work belongs in tests, public documentation, an architecture
  decision, release evidence, or the changelog rather than this register.
