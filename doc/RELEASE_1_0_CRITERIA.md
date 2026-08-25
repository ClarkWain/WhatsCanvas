# WhatsCanvas 1.0 Release Criteria

This document defines the release boundary for WhatsCanvas 1.0. Only the
**Must Have** items block the `v1.0.0` tag. The broader roadmap remains useful,
but it is not the 1.0 acceptance checklist.

## 1.0 Product Contract

WhatsCanvas 1.0 is a C++17, embeddable, Canvas-style 2D rendering library with:

- a documented and stable public C++ header surface;
- Software, OpenGL, OpenGL ES, and Metal delivery, with Vulkan remaining optional;
- multilingual text shaping and font fallback;
- deterministic correctness, packaging, and performance validation; and
- consumable desktop, Android, and iOS SDK artifacts.

Applications remain responsible for windows, input, layout, accessibility,
lifecycle integration, application signing, and store distribution.

## Must Have — Blocks 1.0

| Area | Acceptance evidence | Current state |
|---|---|---|
| Public API freeze | `include/wsc/` is reviewed, `API_STABILITY.md` defines the 1.x policy, and the generated API reference is current. | Complete for 1.0.0. |
| Desktop SDK delivery | Windows, Linux, and macOS Release artifacts contain headers, libraries, CMake package files, and pass a clean consumer build. | Windows 1.0 RC and both consumers verified; hosted Linux/macOS builds run after push. |
| Android SDK delivery | A versioned Prefab AAR contains public headers, `arm64-v8a`, `armeabi-v7a`, and `x86_64` libraries, and passes a clean consumer build. | Local three-ABI 1.0 artifact verified; clean hosted build runs after push. |
| iOS SDK delivery | A versioned XCFramework contains public headers plus device and simulator slices, and passes clean device/simulator consumer builds. | Packaging and dual-slice consumer gates implemented; macOS build runs after push. |
| Blocking validation | API, version, performance, unit, package-consumer, sanitizer, fuzz, backend, and mobile SDK gates required by the release checklist are green. | Local release preflight is green; hosted release-commit CI runs after push. |
| Representative device sign-off | At least one supported Android device and one supported iOS device pass the canonical scene, multilingual text/color emoji, resize/orientation, background/resume, and sustained-frame-pacing checks. | Accepted from prior multi-device testing; the maintainer deferred repeat testing until after 1.0. |
| Reproducible version metadata | CMake, `Version.h`, Android metadata, changelog, documentation snippets, tag, and artifact names consistently identify `1.0.0`. | Complete in the release candidate; tag is created after merge. |
| Consumer documentation | Desktop, Android, and iOS integration paths identify required dependencies, supported architectures, artifact contents, and a minimal consumer. | Complete. |
| Security and dependency review | A vulnerability reporting policy exists and bundled/runtime dependencies receive a final license and known-vulnerability review. | Complete; see `DEPENDENCY_AUDIT_1_0.md`. |

## Should Have — Does Not Block 1.0

- A managed Android emulator in CI in addition to compile/link consumer checks.
- A wider physical-device and GPU matrix than the representative sign-off set.
- Maven Central, CocoaPods, or Swift Package Manager publication.
- Broader Vulkan feature and driver coverage.
- A prebuilt Web distribution archive.

## Explicitly Out of Scope for 1.0

- WebGPU and Direct3D backends.
- UI widgets, layout, input, accessibility, and application lifecycle frameworks.
- A browser-grade rich-text editor or PDF/document engine.
- Full color-management workflows.
- Exhaustive support for every font container and color-font format.
- Binary compatibility across arbitrary compilers, standard libraries, NDKs,
  architectures, dependency versions, or build options.

## Final Go/No-Go

Before tagging `v1.0.0`:

1. Complete `doc/RELEASE_CHECKLIST.md` against the release commit.
2. Link the green workflow runs used as release evidence.
3. Record the device sign-off evidence or an explicit maintainer decision to defer repeat testing.
4. Install or unpack every release asset and build its clean minimal consumer.
5. Confirm that every Must Have row has objective evidence and no open blocker.
