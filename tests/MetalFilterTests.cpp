// Metal Gaussian blur test. Filters an opaque red square via
// filterImageResource with a moderate blur radius and asserts:
//   1. The interior of the square remains strongly red (blur preserves colour
//      at the center).
//   2. Pixels several pixels outside the original square become semi-red
//      (colour has bled outward from the source silhouette).
// The comparison is intentionally loose because the exact fall-off depends on
// the Gaussian kernel parameters — the test only checks that a real blur was
// applied vs the fully-transparent output the filter used to return.

#include <algorithm>
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

// Inner shadow filter: the source silhouette is preserved but pixels near the
// top edge of the shape should be darkened (offset (0, 4) shades the top),
// while the shape's centre stays mostly the source colour.
bool testMetalInnerShadow()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }

    const int w = 96;
    const int h = 96;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create(Metal) for inner shadow should succeed")) {
        return false;
    }
    canvas->initializeContext();

    canvas->beginFrame();
    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255));
    LayerOptions options;
    // radius = 8 (=sigma 8/3), positive offsetY casts inset shadow along the
    // top edge; shadow colour black.
    options.setImageFilter(ImageFilter::innerShadow(8.0f, 8.0f, 0.0f, 8.0f, Color(0, 0, 0, 255)));
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)),
                      layerPaint, options);

    // A large yellow square well inside the canvas so the shadow can bleed
    // into the interior without hitting the edge.
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 240, 80, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(20.0f, 20.0f, 76.0f, 76.0f), fill);

    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "inner shadow readPixelsRGBA should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    // Deep interior (dead centre) should keep the yellow source colour.
    const unsigned char *centre = at(48, 48);
    bool ok = expect(centre[0] > 200 && centre[1] > 180 && centre[3] > 200,
                     "inner-shadow centre should stay close to the source yellow");

    // Just below the top edge (well inside the shape but along the shaded
    // seam) should be noticeably darker: G < interior G by a wide margin.
    const unsigned char *nearTop = at(48, 24);
    ok = expect(nearTop[1] + 40 < centre[1],
                "inner shadow should darken pixels near the shaded (top) edge") && ok;
    ok = expect(nearTop[3] > 200,
                "inner-shadow pixels should keep the source silhouette's alpha") && ok;

    // Fully outside the square should still be transparent.
    const unsigned char *outside = at(4, 4);
    ok = expect(outside[3] < 20, "pixels outside the source silhouette must stay transparent") && ok;
    return ok;
}

// Blur with grayscale colour adjustment: saturation 0 must collapse the
// blurred colour to a grey (R ≈ G ≈ B). The centre of the source is a fully
// saturated blue; after saturation=0 it should read back close to grey.
bool testMetalBlurColorAdjust()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    const int w = 64;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create(Metal) should succeed")) {
        return false;
    }
    canvas->initializeContext();

    canvas->beginFrame();
    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255));
    LayerOptions options;
    ImageFilter filter = ImageFilter::blur(2.0f);
    filter.setColorAdjustment(0.0f, 1.0f, 1.0f);
    options.setImageFilter(filter);
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)),
                      layerPaint, options);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(0, 0, 240, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(20.0f, 20.0f, 44.0f, 44.0f), fill);

    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };
    const unsigned char *centre = at(32, 32);
    // With saturation=0 the blue channel should have collapsed toward the
    // luminance of the source (~28 for pure blue). All three channels must be
    // within a tight band.
    int maxCh = std::max({int(centre[0]), int(centre[1]), int(centre[2])});
    int minCh = std::min({int(centre[0]), int(centre[1]), int(centre[2])});
    bool ok = expect(maxCh - minCh <= 20,
                     "saturation=0 must collapse the blur result to (near) grey");
    ok = expect(centre[3] > 200, "alpha should stay opaque after the post pass") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testMetalGaussianBlur() && ok;
    ok = testMetalInnerShadow() && ok;
    ok = testMetalBlurColorAdjust() && ok;
    return ok ? 0 : 1;
}
