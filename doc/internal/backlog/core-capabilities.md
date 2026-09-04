# Active Product Capability Backlog

Status reviewed: 2026-09-04.

This file contains only unresolved product-level outcomes. Completed execution
history lives in `doc/archive/implementation/core-capability-log.md`. Each item
must receive an issue, owner, and target milestone before implementation starts.

## P0 — Distribution completeness

- [ ] Define the supported Web distribution contract: `.wasm`, ES module,
  TypeScript declarations, browser requirements, and versioning policy.
  - Exit: a clean consumer downloads an official artifact and renders the
    documented first frame without building WhatsCanvas from source.
- [ ] Add release automation and a consumer smoke test for the Web artifact.
  - Exit: CI builds, packages, installs, and runs the same artifact published by
    a tagged release.

## P1 — Platform hardening

- [ ] Record target-hardware release sign-off for supported Android and iOS
  configurations instead of relying only on emulator/simulator evidence.
  - Exit: device, OS, GPU/backend, lifecycle, text, and visual-parity evidence
    is stored in a dated validation record.
- [ ] Extend Vulkan window presentation beyond the current Win32 path and cover
  resize plus device-loss recovery.
  - Exit: each claimed platform has an automated or explicitly documented
    presentation gate.
- [ ] Complete browser host glue for remote fonts and repaint/relayout
  notification.
  - Exit: the public Web-font guide has a runnable browser example and an
    automated loading/failure test.

## P2 — Optional product expansion

- [ ] Decide whether a JavaScript-facing Canvas API is a supported product
  surface or only an example-layer convenience wrapper.
  - Exit: an accepted API boundary and lifetime model exist before bindings are
    published.
- [ ] Evaluate WebGPU only after the WebGL 2 distribution and validation gates
  are stable.
  - Exit: a decision record compares maintenance cost, browser coverage, and
    measurable product benefit.

## Backlog rules

- Keep completed tasks out of this file; close their issue and record durable
  behavior in public docs, tests, an architecture decision, or the changelog.
- Do not treat a checkbox as authorization to expand the public API.
- Every performance or rendering item needs a reproducible baseline and a
  correctness gate before implementation.
- Review this file at each release boundary and archive items that are no longer
  aligned with the product direction.
