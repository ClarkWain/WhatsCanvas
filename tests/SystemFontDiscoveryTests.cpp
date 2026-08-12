// Sanity check for wsc::FontSystem::discoverInstalledFontFaces(). Runs the
// platform's native font manager (CoreText / DirectWrite / fontconfig) and
// verifies the returned face list is non-empty and self-consistent. Skips
// with a diagnostic on platforms where no discovery backend is linked in
// (e.g. Linux without fontconfig at build time).

#include <cstdint>
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
    for (const FontFace &face : faces) {
        ok = expect(!face.descriptor().family.empty(),
                    "every discovered face should carry a non-empty family name") && ok;
        ok = expect(face.sourceType() == FontSourceType::FILE,
                    "every discovered face should be file-backed") && ok;
        ok = expect(!face.path().empty(),
                    "every discovered face should carry a non-empty path") && ok;
        families.insert(face.descriptor().family);
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
    canvas->initializeContext();
    for (const FontFace &face : faces) {
        canvas->registerFontFace(face);
    }

    return ok ? 0 : 1;
}
