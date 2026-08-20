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
    const float expectedScale = 1080.0f / 786.0f;
    return expect(near(viewport.width, 786.0f), "landscape reference width")
        && expect(near(viewport.height, 377.0f), "landscape reference height")
        && expect(near(viewport.scale, expectedScale), "landscape uniform scale")
        && expect(near(viewport.offsetX, 0.0f), "landscape centered horizontally")
        && expect(near(viewport.offsetY, 0.0f), "landscape anchored to top");
}

bool testPortraitAspectFit()
{
    const auto viewport = whatscanvas::desktop::makeFeatureShowcaseViewport(
        520.0f, 780.0f);
    const float expectedScale = 780.0f / 759.0f;
    return expect(near(viewport.width, 393.0f), "portrait reference width")
        && expect(near(viewport.height, 759.0f), "portrait reference height")
        && expect(near(viewport.scale, expectedScale), "portrait uniform scale")
        && expect(near(viewport.offsetX,
                       (520.0f - 393.0f * expectedScale) * 0.5f),
                  "portrait centered horizontally")
        && expect(near(viewport.offsetY, 0.0f), "portrait anchored to top");
}

bool testSafeAreaUsesCanonicalContentWindow()
{
    const auto viewport = whatscanvas::scenes::makeCanonicalViewport(
        390.0f, 844.0f, {47.0f, 34.0f, 0.0f, 0.0f});
    const float expectedScale = 390.0f / 393.0f;
    return expect(near(viewport.width, 393.0f), "safe-area portrait width")
        && expect(near(viewport.height, 759.0f), "safe-area portrait height")
        && expect(near(viewport.scale, expectedScale), "safe-area uniform scale")
        && expect(near(viewport.offsetX, 0.0f), "safe-area horizontal origin")
        && expect(near(viewport.offsetY, 47.0f), "safe-area top anchor");
}

} // namespace

int main()
{
    return testLandscapeAspectFit()
        && testPortraitAspectFit()
        && testSafeAreaUsesCanonicalContentWindow() ? 0 : 1;
}
