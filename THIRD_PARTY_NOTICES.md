# Third-Party Notices

WhatsCanvas includes or can be built with the components below. Copyrights
remain with their respective owners. The full license texts distributed with
official packages are under the adjacent `licenses/` directory.

| Component | WhatsCanvas use | License selected for distribution | Source license |
|---|---|---|---|
| FreeType | Font rasterization in GL-family and Android SDK builds | FreeType License | `licenses/freetype/FTL.TXT` |
| HarfBuzz | OpenType shaping in GL-family and Android SDK builds | Old MIT | `licenses/harfbuzz/COPYING` |
| GLFW | Desktop window/context host and examples | zlib/libpng | `licenses/glfw/LICENSE.md` |
| GLM | Header-only graphics math | MIT option | `licenses/glm/copying.txt` |
| stb | Image/font utility implementation | MIT option | `licenses/stb/LICENSE` |
| glad | Generated OpenGL loader | CC0/public-domain option stated by the glad project; the included Khronos platform header carries its own permissive notice | Generated source notice and `include/KHR/khrplatform.h` in the source tree |

NanoVG is used only by the Android comparison demo. It is not linked into the
WhatsCanvas desktop, Android AAR, or iOS XCFramework release libraries, and the
demo APK is not a release asset. NanoVG is licensed under the zlib license.

Fonts used only by tests, benchmarks, examples, or Web assets retain their
upstream licenses in or beside their source directories. They are not included
in the native SDK release artifacts unless an artifact README says otherwise.

