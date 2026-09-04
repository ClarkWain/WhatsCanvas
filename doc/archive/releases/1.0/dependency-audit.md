# WhatsCanvas 1.0 Dependency Audit

Audit date: 2026-08-24

This is the dependency and license review snapshot for `v1.0.0`. It records
the revisions pinned by the release candidate, their role, redistribution
status, and the public security information reviewed on the audit date.

## Pinned Dependencies

| Component | Pinned revision | Identified version | Release-library scope | License result |
|---|---|---|---|---|
| FreeType | `0a0221a1347e2f1e07c395263540026e9a0aa7c7` | `VER-2-14-3` | GL-family and Android SDK font rasterization | Pass — FreeType License selected; full text packaged. |
| HarfBuzz | `56feae4035bdd48f62ba2b8d8c16232d4d89b3a4` | `14.2.1` | GL-family and Android SDK OpenType shaping | Pass with noted upstream watch item; Old MIT text packaged. |
| GLFW | `b00e6a8a88ad1b60c0a045e696301deb92c9a13e` | `3.4-99-gb00e6a8a` | Desktop host/examples; not part of the core public API | Pass — zlib/libpng text packaged. |
| GLM | `6f14f4792a0cde5d0cf2c910506724d61cb95834` | `1.0.0-139-g6f14f479` | Header-only internal math | Pass — MIT option selected; full text packaged. |
| stb | `31c1ad37456438565541f4919958214b6e762fb4` | commit snapshot | Image/font utilities | Pass — MIT option selected; full text packaged. |
| glad | generated 2022-10-23 with glad 0.1.36 | OpenGL 3.3 core loader | Desktop OpenGL loader | Pass — generated-code public-domain/CC0 option recorded in notices; Khronos header retains its notice. |
| NanoVG | `ce3bf745eb2d2dbc14a50bf2446783f691ac4353` | commit snapshot | Android comparison demo only; excluded from SDK libraries and Release assets | Pass — zlib license; no redistribution in 1.0 assets. |

System OpenGL/EGL/GLES, Vulkan, Metal, CoreText, platform frameworks, and the
Android C++ shared runtime are platform/toolchain components. Consumers must
follow the terms supplied with their selected OS, SDK, driver, and toolchain.

## Security Review

- FreeType 2.14.3 is newer than the `<= 2.13.0` affected range documented for
  CVE-2025-27363, and newer than the affected ranges of the older FreeType
  records reviewed in NVD.
- HarfBuzz 14.2.1 is newer than the `< 12.3.0` range listed for
  GHSA-xvjr-f2r9-c7ww.
- The HarfBuzz security overview listed the moderate
  `GHSA-q4gc-p4hh-3765` pre-context heap over-read on 2026-08-18, but its
  detailed advisory and a patched release were not publicly retrievable during
  this audit. WhatsCanvas has one `hb_buffer_add_utf8` call and always supplies
  `item_offset = 0`, the full normalized buffer length, and an owned
  null-terminated input, so it does not request the affected pre-context path.
  Track the next HarfBuzz release and upgrade when the upstream fix is published.
- The upstream GitHub security pages reviewed for FreeType, GLFW, GLM, and stb
  showed no published repository advisories on the audit date. This is a
  point-in-time review, not a guarantee that future issues do not exist.

## Packaging Result

- Desktop install packages contain the WhatsCanvas license, this notice, and
  the full license texts for bundled release-library dependencies.
- Android AARs contain the same material under `META-INF/`.
- iOS archives contain the same material beside the XCFramework.
- `scripts/verify_mobile_release_artifact.py` treats the license and notice
  files as required release-asset structure.

Result: **PASS for the 1.0 release candidate**, with the HarfBuzz upstream
watch item above accepted because the affected call mode is not used.

