#include <cmath>
#include <iostream>

#include "platforms/desktop/src/SceneViewport.h"

namespace {

bool near(float actual, float expected, float tolerance = 0.01f)
{
    return std::abs(actual - expected) <= tolerance;
}

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool testLandscapeAspectFit()
{
    const auto viewport = whatscanvas::desktop::makeFeatureShowcaseViewport(
        1080.0f, 540.0f);
    const float expectedScale = 1080.0f / 800.0f;
    return expect(near(viewport.width, 800.0f), "landscape reference width")
        && expect(near(viewport.height, 400.0f), "landscape reference height")
        && expect(near(viewport.scale, expectedScale), "landscape uniform scale")
        && expect(near(viewport.offsetX, 0.0f), "landscape centered horizontally")
        && expect(near(viewport.offsetY, 0.0f), "landscape anchored to top");
}

bool testPortraitAspectFit()
{
    const auto viewport = whatscanvas::desktop::makeFeatureShowcaseViewport(
        520.0f, 780.0f);
    const float expectedScale = 780.0f / 800.0f;
    return expect(near(viewport.width, 400.0f), "portrait reference width")
        && expect(near(viewport.height, 800.0f), "portrait reference height")
        && expect(near(viewport.scale, expectedScale), "portrait uniform scale")
        && expect(near(viewport.offsetX,
                       (520.0f - 400.0f * expectedScale) * 0.5f),
                  "portrait centered horizontally")
        && expect(near(viewport.offsetY, 0.0f), "portrait anchored to top");
}

bool testSafeAreaUsesCanonicalContentWindow()
{
    const auto viewport = whatscanvas::scenes::makeCanonicalViewport(
        390.0f, 844.0f, {47.0f, 34.0f, 0.0f, 0.0f});
    const float expectedScale = 763.0f / 800.0f;
    return expect(near(viewport.width, 400.0f), "safe-area portrait width")
        && expect(near(viewport.height, 800.0f), "safe-area portrait height")
        && expect(near(viewport.scale, expectedScale), "safe-area uniform scale")
        && expect(near(viewport.offsetX,
                       (390.0f - 400.0f * expectedScale) * 0.5f),
                  "safe-area horizontal origin")
        && expect(near(viewport.offsetY, 47.0f), "safe-area top anchor");
}

bool testStandardCatalog()
{
    using whatscanvas::scenes::ViewportStandard;
    using whatscanvas::scenes::viewportStandardSpec;
    for (ViewportStandard standard : {
             ViewportStandard::Phone2To1,
             ViewportStandard::CompactPhone16To9,
             ViewportStandard::Tablet4To3,
             ViewportStandard::Desktop16To10}) {
        const auto& spec = viewportStandardSpec(standard);
        if (!expect(near(spec.portraitWidth, spec.landscapeHeight),
                    "standard rotated width")) return false;
        if (!expect(near(spec.portraitHeight, spec.landscapeWidth),
                    "standard rotated height")) return false;
    }
    const auto& primary = viewportStandardSpec(ViewportStandard::Phone2To1);
    return expect(primary.id == "phone_2_1", "primary standard id")
        && expect(primary.primaryPixelGate, "primary pixel gate");
}

bool testStandardParsing()
{
    using whatscanvas::scenes::ViewportStandard;
    ViewportStandard standard = ViewportStandard::Phone2To1;
    return expect(whatscanvas::scenes::parseViewportStandard(
                      "tablet_4_3", standard),
                  "known standard parses")
        && expect(standard == ViewportStandard::Tablet4To3,
                  "parsed standard value")
        && expect(!whatscanvas::scenes::parseViewportStandard(
                      "device_of_the_day", standard),
                  "unknown standard is rejected")
        && expect(standard == ViewportStandard::Tablet4To3,
                  "failed parse preserves selected standard");
}

} // namespace

int main()
{
    return testLandscapeAspectFit()
        && testPortraitAspectFit()
        && testSafeAreaUsesCanonicalContentWindow()
        && testStandardCatalog()
        && testStandardParsing() ? 0 : 1;
}
