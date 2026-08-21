# WhatsCanvas WebAssembly platform

The Web platform builds the shared C++ renderer to WebAssembly and presents it
through WebGL 2. It runs the same four canonical validation scenes and
aspect-fit viewport contract as Android, iOS, and Desktop.

## Toolchain

The supported compiler is Emscripten **4.0.22**, the final tagged 2025 release.
The version is pinned so local and CI output remain reproducible.

```sh
platforms/wasm/bootstrap.sh
platforms/wasm/build.sh
platforms/wasm/test.sh
platforms/wasm/serve.sh
```

Open `http://127.0.0.1:8080/`. Do not open `index.html` directly: browsers
require HTTP to load `.wasm` and the preloaded font data reliably.

The generated files live under `out/wasm-web/platforms/wasm/web/` by default.
Set `EMSDK_ROOT` to use a non-default SDK location, or pass a build directory
as the first argument to `build.sh` and `serve.sh`.

## Runtime contract

- A WebGL 2 / OpenGL ES 3 compatible context is required.
- The drawing buffer tracks browser DPR from 1 through 4; CSS pixels remain the
  logical coordinate space used by the canonical viewport.
- Rendering is scheduled by `requestAnimationFrame` for display-rate pacing.
- Window resize, orientation changes, page visibility, and WebGL context
  loss/restoration rebuild the required surface or renderer state.
- Portable Latin, CJK, and emoji-subset font assets are preloaded before
  `main()` so text does not depend on browser or operating-system font access.
- OpenType shaping is enabled so emoji ZWJ, modifier, flag, and keycap
  sequences stay intact across platforms.

For deterministic captures, use `scene`, `time` and `dpr`, for example
`http://127.0.0.1:8080/?scene=text_stress&time=1.25&dpr=2`.
The page publishes readiness and frame data as `window.whatsCanvasDemo`, which
headless browser tests can inspect.

`test.sh` rebuilds the Web target, launches a clean Chrome profile and validates
all fourteen canonical portrait/landscape captures at DPR 3. It also checks resize, visibility
pause/resume, forced WebGL context loss/restoration, a cache-bypassing cold
reload, browser errors, and display-rate frame pacing. Captures and metadata
are written directly into the shared visual-parity directory under `out/`.

## Font assets

The Web demo reuses the repository's Roboto and M+ test fonts. Its compact
Noto Color Emoji asset is a subset of the 2025-08-18 upstream font containing
only the canonical scene's emoji sequences. The subset retains the CBDT/CBLC
bitmap and GSUB shaping tables; its license is in
`web/assets/NotoColorEmoji.LICENSE.txt`.
