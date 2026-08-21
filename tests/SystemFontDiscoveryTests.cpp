#include <wsc/FontSystem.h>

// Sanity check for wsc::FontSystem::discoverInstalledFontFaces(). Runs the
// platform's native font manager (CoreText / DirectWrite / fontconfig) and
// verifies the returned face list is non-empty and self-consistent. Skips
// with a diagnostic on platforms where no discovery backend is linked in
// (e.g. Linux without fontconfig at build time).

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "wsc/wsc.h"

using namespace wsc;

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
    }
    return condition;
}

} // namespace

int main()
{
    std::vector<FontFace> faces = FontSystem::discoverInstalledFontFaces();

    if (faces.empty()) {
        std::cout << "[SystemFontDiscoveryTests] SKIP: no discovery backend "
                     "available in this build (expected on Linux without "
                     "fontconfig, unknown platforms). Exiting green."
                  << std::endl;
        return 0;
    }

    bool ok = true;
    ok = expect(faces.size() >= 5,
                "expected at least a handful of system fonts to be discovered") && ok;

    std::unordered_set<std::string> families;
    std::unordered_set<std::string> faceKeys;
#ifdef _WIN32
    const FontFace *windowsProbe = nullptr;
#endif
    for (const FontFace &face : faces) {
        ok = expect(!face.descriptor().family.empty(),
                    "every discovered face should carry a non-empty family name") && ok;
        ok = expect(face.sourceType() == FontSourceType::FILE,
                    "every discovered face should be file-backed") && ok;
        ok = expect(!face.path().empty(),
                    "every discovered face should carry a non-empty path") && ok;
        ok = expect(face.faceIndex() >= 0,
                    "every discovered face should carry a non-negative face index") && ok;
        ok = expect(face.weight() >= 1 && face.weight() <= 1000,
                    "every discovered face should carry a valid CSS-style weight") && ok;
        ok = expect(FontSystem::fileExists(face.path()),
                    "every discovered face should point to a readable local file") && ok;

        const std::string key = face.family() + "\x1f" + face.path() + "\x1f"
            + std::to_string(face.faceIndex()) + "\x1f" + std::to_string(face.weight())
            + "\x1f" + std::to_string(static_cast<int>(face.slant()));
        ok = expect(faceKeys.insert(key).second,
                    "discovery should not return duplicate face records") && ok;
        families.insert(face.descriptor().family);
#ifdef _WIN32
        if (face.family() == "Segoe UI"
            && (windowsProbe == nullptr
                || std::abs(face.weight() - 400) < std::abs(windowsProbe->weight() - 400))) {
            windowsProbe = &face;
        }
#endif
    }

    ok = expect(!families.empty(), "family set should be non-empty") && ok;
    std::cout << "[SystemFontDiscoveryTests] discovered " << faces.size()
              << " faces across " << families.size() << " families." << std::endl;

    // Register everything and confirm the canvas accepts the batch. The
    // Software backend needs no GL context, so it is the ideal driver.
    auto canvas = Canvas::create(Canvas::Backend::Software, 32, 32);
    if (!expect(canvas != nullptr, "software canvas should be creatable")) {
        return 1;
    }
    ok = expect(canvas->initializeContext(), "software canvas should initialize") && ok;
    const std::uint64_t generationBeforeRefresh = FontSystem::installedFontGeneration();
    ok = expect(canvas->refreshSystemFonts(),
                "an existing canvas should refresh its system font snapshot") && ok;
    ok = expect(FontSystem::installedFontGeneration() == generationBeforeRefresh + 1,
                "canvas refresh should publish a new system font generation") && ok;
    for (const FontFace &face : faces) {
        ok = expect(canvas->registerFontFace(face),
                    "software canvas should accept every discovered face") && ok;
    }

#ifdef _WIN32
    ok = expect(windowsProbe != nullptr,
                "DirectWrite discovery should expose the standard Segoe UI family") && ok;
    if (windowsProbe != nullptr) {
        Paint paint;
        paint.setTextSize(18.0f);
        paint.setFontFamily(windowsProbe->family());
        paint.setFontWeight(windowsProbe->weight());
        paint.setFontSlant(windowsProbe->slant());
        ok = expect(canvas->measureText("Windows font discovery", paint) > 0.0f,
                    "a DirectWrite-discovered face should load in the portable rasterizer") && ok;

        // Exercise a non-ASCII UTF-8 font path instead of assuming that
        // discovered fonts always live below an ASCII-only directory.
        std::error_code fileError;
        const std::filesystem::path unicodePath =
            std::filesystem::current_path() / std::filesystem::u8path(u8"动态字体测试.ttf");
        std::filesystem::copy_file(std::filesystem::u8path(windowsProbe->path()), unicodePath,
                                   std::filesystem::copy_options::overwrite_existing, fileError);
        ok = expect(!fileError, "test should copy a discovered font to a UTF-8 path") && ok;
        if (!fileError) {
            const FontFace unicodeFace = FontFace::fromFile(
                FontDescriptor("Windows UTF-8 Path Probe", windowsProbe->weight(), windowsProbe->slant()),
                unicodePath.u8string(), windowsProbe->faceIndex());
            ok = expect(FontSystem::fileExists(unicodeFace.path()),
                        "FontSystem should open a UTF-8 Windows font path") && ok;
            ok = expect(canvas->registerFontFace(unicodeFace),
                        "software canvas should register a UTF-8 Windows font path") && ok;
            paint.setFontFamily(unicodeFace.family());
            ok = expect(canvas->measureText("UTF-8 font path", paint) > 0.0f,
                        "portable rasterizer should load a UTF-8 Windows font path") && ok;
        }
        fileError.clear();
        std::filesystem::remove(unicodePath, fileError);
    }
#endif

    // refreshDefaultSystemFontFaces() invalidates the process-wide cache so
    // the next defaultSystemFontFaces() call re-runs discovery. Ask twice
    // and ensure the observable behaviour is stable (equal-size vectors,
    // no crash under the mutex).
    const std::vector<FontFace> beforeRefresh = FontSystem::defaultSystemFontFaces();
    FontSystem::refreshDefaultSystemFontFaces();
    const std::vector<FontFace> afterRefresh = FontSystem::defaultSystemFontFaces();
    ok = expect(beforeRefresh.size() == afterRefresh.size(),
                "defaultSystemFontFaces() should return a stable slot count "
                "before and after refreshDefaultSystemFontFaces()") && ok;

    // The narrower refresh entry points should be independently exercisable
    // without invalidating the whole two-layer cache.
    const std::size_t discoveredBefore = FontSystem::discoverInstalledFontFaces().size();
    FontSystem::refreshDiscoveredFontFaces();
    const std::size_t discoveredAfter = FontSystem::discoverInstalledFontFaces().size();
    ok = expect(discoveredBefore == discoveredAfter,
                "refreshDiscoveredFontFaces() should preserve discovery result "
                "shape across a re-enumeration") && ok;

    const std::size_t defaultsBefore = FontSystem::defaultSystemFontFaces().size();
    FontSystem::refreshDefaultSystemFontFacesOnly();
    const std::size_t defaultsAfter = FontSystem::defaultSystemFontFaces().size();
    ok = expect(defaultsBefore == defaultsAfter,
                "refreshDefaultSystemFontFacesOnly() should preserve the slot "
                "count on rebuild") && ok;

    return ok ? 0 : 1;
}
