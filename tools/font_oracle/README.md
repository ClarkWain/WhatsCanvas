# Android font discovery Oracle

This directory contains deterministic, layered compatibility gates for Android
font configuration, matching, table discovery, and raster semantics:

1. `WhatsCanvasAndroidFontOracleProbe` parses a fixture with the production
   WhatsCanvas parser and emits a versioned JSON snapshot.
2. `run_android_font_oracle.py` compares that snapshot with the checked-in
   golden and, when supplied, with an independently built Skia probe.
3. `VerifyAndroidFontOracle.cmake` builds the WhatsCanvas probe before CTest
   runs it, so a stale executable cannot produce a false pass.
4. `SkiaAndroidFontOracleProbe.cpp` is the independent producer. It calls the
   pinned Skia `SkFontMgr_android_parser` directly; it does not link or reuse
   the WhatsCanvas Android parser.
5. `run_android_font_oracle_corpus.py` classifies cross-version fixtures as
   strict equality or declared compatibility extensions. An extension must
   keep differing first at its reviewed JSON path; silently becoming equal or
   drifting somewhere else fails the gate.
6. `SkiaFontScannerProbe.cpp` exercises the pinned bundled FreeType scanner on
   a real Roboto Flex file. Its golden locks face/instance counts, intrinsic
   style, fixed-pitch state, all axis ranges and the default variation position.
7. `SkiaAndroidFontManagerProbe.cpp` feeds a real XML + Roboto Flex file through
   `SkFontMgr_New_Android`. Its golden locks configured-family enumeration,
   intrinsic-style fallback, Latin glyph matching, distinct zh-Hans/ja Source
   Han fallbacks, COLR and CBDT emoji-presentation fallbacks, and a still-missing
   CJK character in isolated mode.
8. `SkiaFontRasterProbe.cpp` renders the selected Latin, CJK, COLR, and CBDT
   glyphs through pinned Skia/FreeType. Its golden locks glyph metrics, ink
   bounds/counts, pixel hashes, and verifies that both emoji formats actually
   contain colored pixels.
9. `WhatsCanvasFontRasterProbe.cpp` creates the same scene through the production
   portable rasterizer. Its own golden remains engine-specific, while
   `run_font_raster_differential.py` requires matching glyph IDs/advances,
   bounds within one pixel, ink area within three percent, and matching color
   classification. Pixel hashes are reported but deliberately not compared.
10. `WhatsCanvasFontClusterProbe.cpp` accepts a font plus comma-separated hex
    codepoints and reports per-codepoint coverage, HarfBuzz output, and final
    glyph rasterization. The API 33 CN-flag fixture deliberately has no bitmap
    metrics for either regional indicator; its contract only passes when GSUB
    combines the complete cluster and the resulting CBDT glyph rasterizes.

The default test is fully offline:

```powershell
cmake -S . -B build
ctest --test-dir build -C Debug -R WhatsCanvasAndroidFontOracleGolden --output-on-failure
```

The semantic fixture and expected snapshot live in
`tests/fixtures/android_font_oracle/`. The fixture deliberately uses virtual
font paths: this gate tests discovery metadata and candidate ordering, while
font-file parsing and raster output remain covered by their dedicated tests.

## Probe protocol v1

A producer accepts one or more configuration inputs and queries:

```text
probe --config <xml> <font-dir> [--config ...] \
      --query <family|weight|slant|locale|presentation> [--query ...]
```

`slant` is `normal`, `italic`, or `oblique`. `presentation` is `default`,
`emoji`, or `text`. The producer writes exactly one UTF-8 JSON document to
stdout with schema `whatscanvas.android-font-oracle.v1` and these top-level
fields:

- `engine`: producer identity. The in-tree probe must emit `whatscanvas`; the
  independent producer must emit `skia`. The value is verified before it is
  ignored for the semantic cross-engine comparison;
- `faces`: source-order face metadata, including family, locale,
  `fallbackFor`, TTC index, style, variant, and variation axes;
- `aliases`: source-order aliases and optional exact weights;
- `queries`: normalized requests and ordered face indices returned as
  candidates.

All paths below a supplied font directory must be emitted relative to that
directory. A producer must preserve source order and must not inspect whether a
virtual fixture font exists unless the behavior being tested explicitly needs
font-table metadata.

## Strict Skia differential mode

The local Flutter checkout pins Skia revision
`653397c6be15b87fe8f89a4492582fbb825f6da8` in `G:\flutter-master\DEPS`.
WhatsCanvas intentionally does not silently substitute another revision. The
exact source is checked out at
`G:\flutter-master\engine\src\flutter\third_party\skia`, and the standalone
adapter verifies both the Skia and Expat commit IDs during CMake configure.

For a checkout where Flutter has not populated that directory, bootstrap only
the required repositories (the partial clone defers unrelated blobs):

```powershell
git init G:\flutter-master\engine\src\flutter\third_party\skia
git -C G:\flutter-master\engine\src\flutter\third_party\skia remote add origin `
  https://skia.googlesource.com/skia.git
git -C G:\flutter-master\engine\src\flutter\third_party\skia fetch `
  --depth=1 --filter=blob:none origin 653397c6be15b87fe8f89a4492582fbb825f6da8
git -C G:\flutter-master\engine\src\flutter\third_party\skia checkout --detach FETCH_HEAD

git init G:\flutter-master\engine\src\flutter\third_party\skia\third_party\externals\expat
git -C G:\flutter-master\engine\src\flutter\third_party\skia\third_party\externals\expat `
  remote add origin https://chromium.googlesource.com/external/github.com/libexpat/libexpat.git
git -C G:\flutter-master\engine\src\flutter\third_party\skia\third_party\externals\expat `
  fetch --depth=1 --filter=blob:none origin 6154446fccefbf3ca644894f598969113b0c7bcd
git -C G:\flutter-master\engine\src\flutter\third_party\skia\third_party\externals\expat `
  checkout --detach FETCH_HEAD
```

Run Skia's `bin/fetch-gn` and `bin/fetch-ninja` with a working Python 3, then
copy `tools/font_oracle/skia/args.gn` to Skia's `out/font-oracle/args.gn`, run
`bin/gn gen out/font-oracle`, and build the two GN targets:

```powershell
third_party\ninja\ninja.exe -C out\font-oracle skia fontmgr_android_parser
```

The checked-in GN args also require the exact Skia-DEPS revisions of FreeType
`264b5fbf5b912b39f98d038bf75d39be0a73f21b`, libpng
`d5515b5b8be3901aac04e5bd8bd5c89f287bcd33`, and zlib
`646b7f569718921d7d4b5b8e22572ff6c76f2596` under
`third_party/externals/`. The standalone CMake configure rejects any mismatch.

The minimal GN build used by this checkout enables the Android font manager,
Expat and the pinned bundled FreeType scanner, while disabling GPU backends,
image codecs, ICU and tools.
It must provide these artifacts under `out/font-oracle`: `skia.lib`,
`expat.lib`, `freetype2.lib`, `libpng.lib`, `zlib.lib`, and
`obj/src/ports/fontmgr_android_parser.SkFontMgr_android_parser.obj`.

Build the independent producer from a Visual Studio developer shell:

```powershell
cmake -S tools/font_oracle/skia -B build-skia-font-oracle -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DSKIA_SOURCE_DIR=G:/flutter-master/engine/src/flutter/third_party/skia `
  -DSKIA_BUILD_DIR=G:/flutter-master/engine/src/flutter/third_party/skia/out/font-oracle
cmake --build build-skia-font-oracle
```

Then configure its absolute path in the main build:

```powershell
cmake -S . -B build-oracle `
  -DWHATSCANVAS_ENABLE_EXTERNAL_FONT_ORACLES=ON `
  -DWHATSCANVAS_SKIA_FONT_ORACLE_EXECUTABLE=$PWD/build-skia-font-oracle/SkiaAndroidFontOracleProbe.exe `
  -DWHATSCANVAS_SKIA_FONT_SCANNER_EXECUTABLE=$PWD/build-skia-font-oracle/SkiaFontScannerProbe.exe `
  -DWHATSCANVAS_SKIA_ANDROID_FONT_MANAGER_EXECUTABLE=$PWD/build-skia-font-oracle/SkiaAndroidFontManagerProbe.exe `
  -DWHATSCANVAS_SKIA_FONT_RASTER_EXECUTABLE=$PWD/build-skia-font-oracle/SkiaFontRasterProbe.exe
ctest --test-dir build-oracle -C Debug `
  -R "WhatsCanvas(AndroidFontOracle|Skia)" --output-on-failure
```

The neutral opt-in keeps reference-engine settings out of ordinary core builds;
the engine-specific cache variables and tests are declared only by
`tools/font_oracle/cmake/WhatsCanvasFontOracleTests.cmake`. When enabled and a
probe path is set, CMake registers the corresponding strict test. A
missing executable, non-zero exit, invalid JSON, schema mismatch, or first
semantic difference fails the test. The runner writes the two normalized
artifacts to `<build>/android-font-oracle/` for diagnosis. It never reports
dual-engine success when only the WhatsCanvas producer ran.

The current fixed-revision dual-engine gate passes on Windows/MSVC. The fixture
uses virtual font paths deliberately, so the Skia producer consumes parser
metadata without asking `SkFontMgr` to open font files. Alias records are read
with Skia's pinned Expat dependency because Skia's parsed family structure
intentionally folds unweighted aliases into names and materializes weighted
aliases as derived families. Face, fallback, locale, variant, TTC and axis
metadata still come from the Skia parser itself.

The corpus gate currently locks five exact-equality cases (API 21 family-list
and case-insensitive aliases, API 23 weighted aliases, API 29 locale metadata,
and API 33 presentation metadata) plus six reviewed
WhatsCanvas extensions: per-file legacy metadata, duplicate-axis rejection,
`oblique` XML support, empty-font filtering, and comma-separated BCP-47 locale
normalization exposed by the complete AOSP API 33 and MIUI API 30 captures.
The complete captures cover 367/360 faces respectively, including AOSP
variable Roboto/Noto fallbacks and MIUI MiLan instances. The classification lives in
`tests/fixtures/android_font_oracle/corpus_manifest.json`; every extension must
include both an expected first-difference path and a rationale.

`WhatsCanvasSkiaFontScannerGolden` is a separate real-file gate. It does not
pretend that parser-only virtual paths validate font tables: the pinned
FreeType scanner must actually open the bundled Roboto Flex fixture and match
`roboto_flex.skia-scanner.expected.json`.
`WhatsCanvasSkiaAndroidFontManagerGolden` goes one layer higher and requires the
actual Android font manager to instantiate that file, resolve a deliberately
mismatched `700 italic` request to its real `400 normal` style, return the Latin
`A` glyph, choose distinct bundled Source Han subsets for U+4C2E under
`zh-Hans` and `ja`, choose the bundled Noto Color Emoji subset for U+2049 under
`und-Zsye`, and reject the still-unsupported U+4E2D in isolated mode. These
fallbacks remain hidden from normal family enumeration, matching Skia's manager
behavior.
`WhatsCanvasSkiaFontRasterGolden` then renders the four selected glyphs at a
fixed size with fixed hinting/subpixel settings. This is a revision- and
toolchain-pinned Skia reference baseline, not a requirement that WhatsCanvas
and Skia produce byte-identical pixels. Cross-engine comparison should first
use glyph availability, metrics, bounds, and color/non-color classification;
engine-specific pixel hashes remain separate regression contracts.
`WhatsCanvasFontRasterGolden` locks the production rasterizer independently.
`WhatsCanvasFontRasterDifferential` confirms structural parity for Latin, CJK,
COLR/CPAL, and the common CBDT/CBLC index-format-1 + image-format-17 PNG path.
Bundled FreeType still has libpng disabled; WhatsCanvas validates table offsets
and uses its existing stb PNG decoder for that fallback. A change on either
side forces review instead of silently widening the compatibility claim.
