// Metal vs Software filter pixel-parity test. Draws the same red rectangle
// inside a saveLayer(blur(4)) on both the Metal and Software backends and
// asserts the readback pixel channels stay within a modest tolerance at
// several probe points. Catches Metal-only regressions in the blur kernel
// weights, the InnerShadow / Blur uniform layout, and the layer compose path
// by comparing directly against the reference CPU implementation instead of
// hand-picked colour bands.

#include <algorithm>
#include <cmath>
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

std::vector<unsigned char> renderBlurScene(Canvas::Backend backend, int w, int h)
{
    auto canvas = Canvas::create(backend, w, h);
    if (!canvas) return {};
    canvas->initializeContext();

    canvas->beginFrame();
    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255));
    LayerOptions options;
    options.setImageFilter(ImageFilter::blur(4.0f));
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)),
                      layerPaint, options);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 0, 0, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(24.0f, 24.0f, 16.0f, 16.0f), fill);

    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    canvas->readPixelsRGBA(pixels);
    return pixels;
}

bool testMetalBlurParityVsSoftware()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    const int w = 64;
    const int h = 64;
    std::vector<unsigned char> metalPixels = renderBlurScene(Canvas::Backend::Metal, w, h);
    if (!expect(!metalPixels.empty(), "Metal render should produce pixels")) {
        return false;
    }
    std::vector<unsigned char> softwarePixels = renderBlurScene(Canvas::Backend::Software, w, h);
    if (!expect(!softwarePixels.empty(), "Software render should produce pixels")) {
        return false;
    }

    struct Probe { int x, y; const char *name; };
    const Probe probes[] = {
        {32, 32, "centre"},
        {24, 32, "left interior edge"},
        {40, 32, "right interior edge"},
        {20, 32, "left blurred halo"},
        {44, 32, "right blurred halo"},
        {8, 8, "far corner"},
    };
    bool ok = true;
    for (const Probe &probe : probes) {
        const std::size_t idx = (static_cast<std::size_t>(probe.y) * w + probe.x) * 4u;
        for (int channel = 0; channel < 4; ++channel) {
            const int mp = metalPixels[idx + channel];
            const int sp = softwarePixels[idx + channel];
            const int delta = std::abs(mp - sp);
            // Tolerate ~10% per-channel drift: Software uses an integer CPU
            // convolution while Metal runs a Gaussian in fp32 with the linear
            // sampler, so the two never match bit-for-bit but should agree on
            // the qualitative shape of the blur.
            const bool ok_here = delta <= 32;
            if (!ok_here) {
                std::cerr << "  probe " << probe.name << " channel " << channel
                          << ": metal=" << mp << " software=" << sp
                          << " delta=" << delta << std::endl;
            }
            ok = expect(ok_here,
                        std::string("blur parity probe '") + probe.name
                            + "' should match within tolerance") && ok;
        }
    }
    return ok;
}

} // namespace

int main()
{
    return testMetalBlurParityVsSoftware() ? 0 : 1;
}
