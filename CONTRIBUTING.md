# Contributing to WhatsCanvas

Thanks for your interest in improving WhatsCanvas. This guide covers the local
workflow, the checks that gate a pull request, and a few conventions specific to
this repository.

## Prerequisites

- **CMake** 3.16 or newer, and a **C++17** compiler.
- **Windows:** Visual Studio 2022 with the Desktop C++ workload.
- **macOS / Linux:** an OpenGL development environment plus the system graphics
  dev libraries needed to build the GLFW examples.
- **Python 3** (for the docs site and a few validation scripts).

Clone with submodules (third-party sources live under `third_party/`):

```sh
git clone --recursive https://github.com/ClarkWain/WhatsCanvas.git
```

## Build

```bat
build.bat --no-run     :: Windows: configure + build (omit --no-run to also run the demo)
```

```bash
./build.sh --no-run    # macOS / Linux
```

Optional feature flags: `-DWHATSCANVAS_BUILD_OPENGLES=ON`,
`-DWHATSCANVAS_BUILD_SOFTWARE=ON`, `-DWHATSCANVAS_ENABLE_VULKAN=ON`,
`-DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON`,
`-DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON`.

## Test

Run the headless unit suite (no GPU/window required):

```bat
ctest --test-dir build --output-on-failure -C Debug -L unit
```

Prefer adding **headless, deterministic** tests. Patterns already in the repo:

- Pure-logic and command-recording tests via a fake renderer
  (`tests/ContextLifecycleTests.cpp`).
- Golden-image regression on the software backend
  (`tests/SoftwareGoldenTests.cpp`; regenerate baselines with
  `WHATSCANVAS_UPDATE_SOFTWARE_BASELINES=1`).

## Pre-PR checklist

Run the fast local preflight before opening a PR:

```bat
cmd /c scripts\release_preflight.bat
```

It covers the checks most often missed:

- **API reference freshness** — the generated `doc/API_REFERENCE.md` must match
  the public headers. If you add or change anything in `include/wsc/`, regenerate:
  ```bat
  python scripts/generate_api_reference.py
  ```
- **Version consistency** — the version in `CMakeLists.txt`, `include/wsc/Version.h`,
  the `find_package` snippets in `README.md` and `doc/GETTING_STARTED_AS_LIBRARY.md`,
  and the package workflow must all agree.
- **Unit tests** and **package-consumer smoke**.

CI (`.github/workflows/cross-platform-validation.yml`) additionally builds on
Windows / macOS / Linux, runs the OpenGL ES build smoke, and — when Vulkan is
enabled — a Vulkan build gate.

## Repository conventions

- **New library sources are listed explicitly**, not globbed. When you add a
  `.cpp` under `src/`, add it to the source list in
  `cmake/WhatsCanvasOpenGL.cmake` (and the software list if it is backend-neutral).
- **Public API** lives in `include/wsc/`. Keep the surface small and documented;
  see `doc/API_STABILITY.md` for the stability boundary.
- **The library owns no window/context and no file I/O** — keep that contract.
- Match the surrounding code style; keep changes focused and reviewable.

## Where things live

- `src/` — core implementation (canvas, command, render, opengl, vulkan, text).
- `include/wsc/` — public headers (the consumer surface).
- `examples/` — demo + game examples (GLFW windows).
- `tests/` — unit tests and the runnable `package_consumer`.
- `benchmarks/` — core benchmarks.
- `scripts/` — build/smoke/regression/validation scripts.
- `doc/` — guides, matrices, ADRs; `mkdocs.yml` builds the docs site from it.

## Pull requests

1. Branch off `master`, keep commits focused (one logical change each).
2. Ensure the pre-PR checklist and `ctest -L unit` pass locally.
3. Open the PR with a clear summary of *what* and *why*, and how you verified it.
4. Address review feedback (CI re-runs on each push); PRs merge once CI is green
   and review is resolved.

By contributing, you agree that your contributions are licensed under the same
license as this project (see `LICENSE`).
