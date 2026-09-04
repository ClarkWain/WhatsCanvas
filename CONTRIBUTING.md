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

Windows:

```bat
build.bat --no-run
```

macOS / Linux:

```bash
sh ./build.sh --no-run
```

Optional feature flags: `-DWHATSCANVAS_BUILD_OPENGLES=ON`,
`-DWHATSCANVAS_BUILD_SOFTWARE=ON`, `-DWHATSCANVAS_ENABLE_VULKAN=ON`.
FreeType rasterization and HarfBuzz shaping default to `ON`; use
`-DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=OFF` or
`-DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=OFF` to test their fallback paths.

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

## Safety toolchain

The repository ships an automated bug-detection stack that CI runs on every
push and PR. Failures here are the fastest way to catch a regression; the
same checks are cheap to run locally before you push.

### Runtime sanitizers

`.github/workflows/cross-platform-validation.yml` has two Linux CI jobs:

- `sanitizers` — `-fsanitize=address,undefined`. Catches out-of-bounds
  reads/writes, use-after-free, leaks, signed overflow, invalid enums.
- `thread-sanitizer` — `-fsanitize=thread`. Catches data races on
  `thread_local` state (`DrawPathCommandPool`, `FontRasterizer::fontData`
  snapshot) and multi-threaded command submission.

Reproduce locally on any clang/gcc host (macOS or Linux):

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
    -DWHATSCANVAS_ENABLE_STDLIB_HARDENING=ON \
    -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-asan -j
ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    ctest --test-dir build-asan -C Debug -L unit --output-on-failure
```

### Standard-library hardening

`-DWHATSCANVAS_ENABLE_STDLIB_HARDENING=ON` (Debug only) turns `std::vector`,
`std::string`, `std::span`, and iterator arithmetic into aborting checks by
defining `_GLIBCXX_ASSERTIONS` (libstdc++) or `_LIBCPP_HARDENING_MODE=2`
(libc++). The sanitizers jobs above enable it; Release builds are unaffected.

If you use Apple LLVM 20+ (Xcode 26+) note that the legacy
`_LIBCPP_ENABLE_HARDENED_MODE` / `_LIBCPP_ENABLE_ASSERTIONS` macros are hard
errors; the new integer macro is required and is what the CMake option
defines.

### Static analysis: clang-tidy (incremental)

`.github/workflows/clang-tidy.yml` runs on every PR and lints only the
`.cpp`/`.mm` files that the PR touches. It uses `.clang-tidy` at repo root,
enables `bugprone-*` / `cert-*` / `clang-analyzer-*`, and runs with
`--warnings-as-errors='*'` so any new diagnostic blocks the PR.

Reproduce locally (requires LLVM 15+; on macOS `brew install llvm`):

```bash
# 1. Emit compile_commands.json (once per configure).
cmake -S . -B build-tidy -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DWHATSCANVAS_BUILD_DEMO=OFF -DWHATSCANVAS_BUILD_BENCHMARKS=OFF
cmake --build build-tidy -j

# 2. Full-tree lint (matches the baseline the CI expects to remain clean).
/opt/homebrew/opt/llvm/bin/run-clang-tidy \
    -clang-tidy-binary /opt/homebrew/opt/llvm/bin/clang-tidy \
    -p build-tidy -quiet -j 8 \
    'src/(canvas|command|opengl|render|text|core)/[^/]+\.(cpp|mm)$'

# 3. Just the files you touched (matches CI's incremental behavior).
git diff --name-only --diff-filter=AM origin/master...HEAD \
    | grep -E '^(src|platforms)/.*\.(cpp|mm)$' \
    | xargs /opt/homebrew/opt/llvm/bin/clang-tidy \
        -p build-tidy --quiet --warnings-as-errors='*'
```

The baseline on branch `1.1.0` was cleaned to **0 diagnostics**. If a check
fires on your PR, prefer fixing the code. When a diagnostic is a genuine
false positive, suppress it at the exact call site with a rationale:

```cpp
// NOLINTNEXTLINE(bugprone-use-after-move) : the object below is a fresh
// initialization of `state`, not a use of the moved-from prior state.
state = build_new_state();
```

Do **not** widen a check disable in `.clang-tidy` without documenting the
suppression in the per-check rationale block in that file.

### Static analysis: CodeQL

`.github/workflows/codeql.yml` runs GitHub's `security-and-quality` C++ query
pack on every push, on every PR, and weekly at 06:00 UTC Monday. Findings
appear on the repository Security tab. No local reproduction is needed
before pushing; treat the Security tab as authoritative.

### Fuzzing

`.github/workflows/cross-platform-validation.yml` runs the
`WhatsCanvasTextAndFontConfigFuzzer` libFuzzer target for 20 000 iterations
on each push/PR. Locally:

```bash
cmake -S . -B build-fuzz -DCMAKE_BUILD_TYPE=Debug -DWHATSCANVAS_BUILD_FUZZERS=ON \
    -DCMAKE_C_FLAGS='-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer' \
    -DCMAKE_CXX_FLAGS='-fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer'
cmake --build build-fuzz -j --target WhatsCanvasTextAndFontConfigFuzzer
./build-fuzz/WhatsCanvasTextAndFontConfigFuzzer tests/fuzz/corpus/text \
    -runs=20000 -max_len=1048576 -timeout=10
```

## Pre-PR checklist

Run the fast local preflight before opening a PR:

```bat
cmd /c scripts\release_preflight.bat
```

It covers the checks most often missed:

- **API reference freshness** — the generated `doc/public/reference/API_REFERENCE.md` must match
  the public headers. If you add or change anything in `include/wsc/`, regenerate:
  ```bat
  python scripts/generate_api_reference.py
  ```
- **Version consistency** — the version in `CMakeLists.txt`, `include/wsc/Version.h`,
  the `find_package` snippets in `README.md` and `doc/public/getting-started/GETTING_STARTED_AS_LIBRARY.md`,
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
  see `doc/public/reference/API_STABILITY.md` for the stability boundary.
- **The library owns no window/context and no file I/O** — keep that contract.
- Match the surrounding code style; keep changes focused and reviewable.

## Where things live

- `src/` — core implementation (canvas, command, render, opengl, vulkan, text).
- `include/wsc/` — public headers (the consumer surface).
- `examples/` — focused starter, package-consumer, game, and presentation hosts.
- `tests/` — unit, integration, compile-contract, and visual-regression tests.
- `benchmarks/` — core benchmarks.
- `scripts/` — build/smoke/regression/validation scripts.
- `doc/public/` — current user documentation; `mkdocs.yml` builds the site from it.
- `doc/internal/` — maintainer architecture, optional-work registers, reviews, validation,
  and operations. Follow the
  [documentation governance policy](doc/internal/operations/documentation-governance.md).
- `doc/archive/` — completed release evidence and implementation history.

## Pull requests

1. Branch off `master`, keep commits focused (one logical change each).
2. Ensure the pre-PR checklist and `ctest -L unit` pass locally.
3. Open the PR with a clear summary of *what* and *why*, and how you verified it.
4. Address review feedback (CI re-runs on each push); PRs merge once CI is green
   and review is resolved.

By contributing, you agree that your contributions are licensed under the same
license as this project (see `LICENSE`).
