// Metal Gaussian blur test. Filters an opaque red square via
// filterImageResource with a moderate blur radius and asserts:
//   1. The interior of the square remains strongly red (blur preserves colour
//      at the center).
//   2. Pixels several pixels outside the original square become semi-red
//      (colour has bled outward from the source silhouette).
// The comparison is intentionally loose because the exact fall-off depends on
// the Gaussian kernel parameters — the test only checks that a real blur was
// applied vs the fully-transparent output the filter used to return.

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
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

bool testMetalGaussianBlur()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        std::cout << "Metal unavailable in this build: skipping blur test.\n";
        return true;
    }

    const int w = 64;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create(Metal) should succeed")) {
        return false;
    }
    canvas->initializeContext();

    // The image-filter path is easiest to drive through a saveLayer that
    // applies a blur: the layer's content is rendered, sampled by the filter,
    // and blitted back to the main target.
    canvas->beginFrame();
    Paint layerPaint;
    // Paint defaults to opaque black; for a passthrough composite of a layer
    // we want white so the sampled image survives the tint multiply.
    layerPaint.setColor(Color(255, 255, 255, 255));
    LayerOptions options;
    options.setImageFilter(ImageFilter::blur(6.0f));
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)),
                      layerPaint, options);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 0, 0, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(24.0f, 24.0f, 40.0f, 40.0f), fill);

    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    // Interior of the original 16x16 square should still be near-fully red.
    const unsigned char *center = at(32, 32);
    bool ok = expect(center[0] > 150 && center[1] < 60 && center[2] < 60 && center[3] > 150,
                     "blurred square center should stay strongly red");

    // Halo probe: pixels beyond the source edge should have a nonzero
    // red component from the outward bleed. Sharp fills would report r=0.
    const unsigned char *halo = at(46, 32);
    ok = expect(halo[0] > 8, "blur should bleed red into pixels outside the source square") && ok;

    // Far-away corner should still be mostly untouched (near transparent).
    const unsigned char *corner = at(2, 2);
    ok = expect(corner[3] < 20, "far corner should be untouched by the blur") && ok;
    return ok;
}

} // namespace

int main()
{
    return testMetalGaussianBlur() ? 0 : 1;
}
