// Metal shadow variants + clip/text interaction tests. Rounds out the
// coverage picture: exercises InnerShadow with several offset directions
// under a single test and verifies text renders correctly under a scissor
// clip via clipRect.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "wsc/wsc.h"

#include "command/DrawCommand.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/metal/MetalRenderDevice.h"

using namespace wsc;

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
    }
    return condition;
}

bool testMetalRgbaSilhouetteShadowUsesAlpha()
{
    if (!MetalRenderDevice::isAvailable()) {
        return true;
    }
    MetalRenderDevice device;
    device.initializeBackend();
    if (!expect(device.isDeviceReady(), "Metal device should initialize")) {
        return false;
    }
    constexpr int width = 48;
    constexpr int height = 48;
    auto target = device.createRenderTarget(width, height);
    if (!expect(target && target->isValid(),
                "RGBA silhouette shadow target should initialize")) {
        return false;
    }

    // Opaque blue has a zero red channel. A shadow implementation that treats
    // red as coverage instead of alpha makes this silhouette disappear.
    std::vector<unsigned char> blueRgba(2u * 2u * 4u, 0);
    for (std::size_t pixel = 0; pixel < 4; ++pixel) {
        blueRgba[pixel * 4u + 2] = 255;
        blueRgba[pixel * 4u + 3] = 255;
    }
    SharedImageResource image = device.createImageResourceRGBA(
        2, 2, blueRgba);
    if (!expect(image && image->isValid(),
                "RGBA silhouette image should upload")) {
        return false;
    }

    DrawImageData quad;
    quad.imageResource = image;
    quad.x = 18.0f;
    quad.y = 18.0f;
    quad.width = 12.0f;
    quad.height = 12.0f;
    DrawShadowData shadow;
    shadow.blurRadius = 4.0f;
    shadow.color[0] = 0.0f;
    shadow.color[1] = 1.0f;
    shadow.color[2] = 0.0f;
    shadow.color[3] = 1.0f;
    shadow.canvasWidth = width;
    shadow.canvasHeight = height;
    shadow.imageSilhouette.push_back(quad);
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawShadowCommand>(shadow));
    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;
    if (!expect(device.executeCommands(target, commands, request),
                "RGBA silhouette shadow should execute")) {
        return false;
    }
    std::vector<unsigned char> pixels;
    if (!expect(device.readPixelsRGBA(width, height, pixels),
                "RGBA silhouette shadow should read back")) {
        return false;
    }
    const std::size_t center = (24u * width + 24u) * 4u;
    return expect(pixels[center + 3] > 150,
                  "opaque RGBA silhouette alpha should cast a shadow")
        && expect(pixels[center + 1] > 150 && pixels[center] < 30,
                  "RGBA silhouette shadow should retain its green tint");
}

struct ShadowProbe
{
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    int shadedProbeX = 0;
    int shadedProbeY = 0;
    const char *edge = "";
};

bool renderDropShadow(Canvas::Backend backend, std::vector<unsigned char> &pixels)
{
    constexpr int width = 64;
    constexpr int height = 64;
    auto canvas = Canvas::create(backend, width, height);
    if (!canvas) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    Paint paint;
    paint.setStyle(Paint::Style::FILL);
    paint.setColor(Color(255, 210, 40, 255));
    paint.setAntiAlias(false);
    paint.setShadowLayer(6.0f, 6.0f, 4.0f, Color(20, 40, 255, 192));
    canvas->drawRect(RectF(18.0f, 18.0f, 16.0f, 16.0f), paint);
    canvas->endFrame();
    return canvas->readPixelsRGBA(pixels);
}

bool testMetalGaussianDropShadow()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    constexpr int width = 64;
    std::vector<unsigned char> metal;
    std::vector<unsigned char> software;
    bool ok = expect(renderDropShadow(Canvas::Backend::Metal, metal),
                     "Metal Gaussian drop-shadow scene should render");
    ok = expect(renderDropShadow(Canvas::Backend::Software, software),
                "Software Gaussian drop-shadow reference should render") && ok;
    if (!ok || metal.size() != software.size()) {
        return false;
    }

    auto at = [width](const std::vector<unsigned char> &pixels, int x, int y) {
        return &pixels[(static_cast<std::size_t>(y) * width + x) * 4u];
    };
    // The offset hard silhouette ends at x=40. Coverage at x=42 can therefore
    // only come from the Gaussian feather, not the former hard-shadow fallback.
    const unsigned char *softEdge = at(metal, 42, 30);
    const unsigned char *fartherEdge = at(metal, 45, 30);
    const unsigned char *farCorner = at(metal, 2, 2);
    ok = expect(softEdge[3] > 3 && softEdge[3] < 180,
                "drop shadow should have partial alpha outside its hard silhouette; alpha="
                    + std::to_string(softEdge[3])) && ok;
    ok = expect(softEdge[2] > softEdge[0] + 80,
                "blurred drop-shadow feather should retain its blue tint; rgba="
                    + std::to_string(softEdge[0]) + ","
                    + std::to_string(softEdge[1]) + ","
                    + std::to_string(softEdge[2]) + ","
                    + std::to_string(softEdge[3])) && ok;
    ok = expect(fartherEdge[3] < softEdge[3],
                "drop-shadow alpha should decay away from the silhouette; near="
                    + std::to_string(softEdge[3]) + " far="
                    + std::to_string(fartherEdge[3])) && ok;
    ok = expect(farCorner[3] < 4,
                "drop shadow must not contaminate a distant transparent corner") && ok;

    std::uint64_t alphaError = 0;
    int compared = 0;
    for (std::size_t i = 0; i < metal.size(); i += 4) {
        if (metal[i + 3] > 0 || software[i + 3] > 0) {
            alphaError += static_cast<std::uint64_t>(
                std::abs(static_cast<int>(metal[i + 3])
                         - static_cast<int>(software[i + 3])));
            ++compared;
        }
    }
    const double meanAlphaError = compared > 0
        ? static_cast<double>(alphaError) / static_cast<double>(compared)
        : 0.0;
    ok = expect(meanAlphaError < 12.0,
                "Metal drop-shadow alpha should stay close to the Software reference") && ok;
    return ok;
}

bool renderTextShadow(Canvas::Backend backend, bool withShadow,
                      std::vector<unsigned char> &pixels)
{
    constexpr int width = 96;
    constexpr int height = 64;
    auto canvas = Canvas::create(backend, width, height);
    if (!canvas) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();
    Paint paint;
    paint.setColor(Color(245, 245, 245, 255));
    paint.setTextSize(32.0f);
    if (withShadow) {
        paint.setShadowLayer(6.0f, 5.0f, 0.0f,
                             Color(20, 40, 255, 192));
    }
    canvas->drawText("H", 20.0f, 44.0f, paint);
    canvas->endFrame();
    return canvas->readPixelsRGBA(pixels);
}

bool testMetalGaussianTextShadow()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    std::vector<unsigned char> metalShadow;
    std::vector<unsigned char> softwareShadow;
    std::vector<unsigned char> softwarePlain;
    bool ok = expect(renderTextShadow(Canvas::Backend::Metal, true, metalShadow),
                     "Metal text-shadow scene should render");
    ok = expect(renderTextShadow(Canvas::Backend::Software, true, softwareShadow),
                "Software text-shadow reference should render") && ok;
    ok = expect(renderTextShadow(Canvas::Backend::Software, false, softwarePlain),
                "plain Software text reference should render") && ok;
    if (!ok || metalShadow.size() != softwareShadow.size()
        || metalShadow.size() != softwarePlain.size()) {
        return false;
    }

    int referenceFeatherPixels = 0;
    int matchedFeatherPixels = 0;
    std::uint64_t alphaError = 0;
    for (std::size_t i = 0; i < metalShadow.size(); i += 4) {
        alphaError += static_cast<std::uint64_t>(
            std::abs(static_cast<int>(metalShadow[i + 3])
                     - static_cast<int>(softwareShadow[i + 3])));
        // Exclude the original glyph. What remains in the Software reference
        // is the blurred glyph-atlas silhouette, so a hard text shadow cannot
        // satisfy this overlap check merely through glyph antialiasing.
        if (softwarePlain[i + 3] <= 2 && softwareShadow[i + 3] > 4) {
            ++referenceFeatherPixels;
            if (metalShadow[i + 3] > 2) {
                ++matchedFeatherPixels;
            }
        }
    }
    const double overlap = referenceFeatherPixels > 0
        ? static_cast<double>(matchedFeatherPixels)
            / static_cast<double>(referenceFeatherPixels)
        : 0.0;
    const double meanAlphaError = metalShadow.empty()
        ? 0.0
        : static_cast<double>(alphaError)
            / static_cast<double>(metalShadow.size() / 4u);
    ok = expect(referenceFeatherPixels > 20,
                "Software reference should expose a measurable text-shadow feather") && ok;
    ok = expect(overlap > 0.70,
                "Metal text shadow should cover the Software Gaussian feather") && ok;
    ok = expect(meanAlphaError < 8.0,
                "Metal text-shadow alpha should stay close to Software") && ok;
    return ok;
}

// Runs the same yellow-rect / inner-shadow scenario for a chosen offset and
// probes the specified edge pixel. Every direction should darken the
// corresponding edge (positive X shades LEFT, positive Y shades TOP by the
// public API contract).
bool runShadowVariant(const ShadowProbe &probe)
{
    const int w = 64;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, std::string("create(Metal) for ") + probe.edge)) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255));
    LayerOptions options;
    options.setImageFilter(ImageFilter::innerShadow(6.0f, 6.0f, probe.offsetX, probe.offsetY,
                                                    Color(0, 0, 0, 255)));
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)),
                      layerPaint, options);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 240, 80, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(12.0f, 12.0f, 40.0f, 40.0f), fill);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), std::string("readPixels ") + probe.edge)) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };
    const unsigned char *centre = at(32, 32);
    const unsigned char *shaded = at(probe.shadedProbeX, probe.shadedProbeY);
    bool ok = expect(centre[1] > 180,
                     std::string("centre should stay yellow for ") + probe.edge);
    ok = expect(shaded[1] + 40 < centre[1],
                std::string("edge should be darkened for offset direction ") + probe.edge) && ok;
    return ok;
}

bool testMetalShadowDirections()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    bool ok = true;
    ok = runShadowVariant({/*offX=*/6.0f, /*offY=*/0.0f, /*probeX=*/16, /*probeY=*/32, "left"}) && ok;
    ok = runShadowVariant({-6.0f, 0.0f, 48, 32, "right"}) && ok;
    ok = runShadowVariant({0.0f, 6.0f, 32, 16, "top"}) && ok;
    ok = runShadowVariant({0.0f, -6.0f, 32, 48, "bottom"}) && ok;
    return ok;
}

// Clip + text: draw an ASCII glyph under a clipRect that clips out the right
// half of the canvas. The glyph should still render on the left half but any
// alpha8 texel that falls on the right must be culled by the scissor.
bool testMetalClipTextInteraction()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    const int w = 96;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create(Metal) should succeed")) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    canvas->save();
    canvas->clipRect(RectF(0.0f, 0.0f, 48.0f, static_cast<float>(h)));
    Paint textPaint;
    textPaint.setColor(Color(0, 200, 0, 255));
    textPaint.setTextSize(32.0f);
    canvas->drawText("H", 8.0f, 44.0f, textPaint);
    canvas->drawText("R", 60.0f, 44.0f, textPaint);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }

    int leftGreenPixels = 0;
    int rightGreenPixels = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = &pixels[(static_cast<std::size_t>(y) * w + x) * 4u];
            const bool green = p[1] > 100 && p[0] < 40 && p[2] < 40 && p[3] > 100;
            if (!green) continue;
            if (x < 48) ++leftGreenPixels; else ++rightGreenPixels;
        }
    }
    bool ok = expect(leftGreenPixels > 20,
                     "the H glyph on the left side of the clip should render");
    ok = expect(rightGreenPixels == 0,
                "no green pixel should survive the scissor clip on the right side") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testMetalRgbaSilhouetteShadowUsesAlpha() && ok;
    ok = testMetalGaussianDropShadow() && ok;
    ok = testMetalGaussianTextShadow() && ok;
    ok = testMetalShadowDirections() && ok;
    ok = testMetalClipTextInteraction() && ok;
    return ok ? 0 : 1;
}
