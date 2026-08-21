// Headless tests for the pure-CPU software rasterizer backend. No GPU or
// graphics context is required, so these run anywhere and assert exact pixels.

#include <iostream>
#include <string>
#include <vector>

#include "wsc/wsc.h"

using namespace wsc;

namespace {

std::unique_ptr<Canvas> makeSoftwareCanvas(int w, int h)
{
    auto c = Canvas::create(Canvas::Backend::Software, w, h);
    if (c) c->initializeContext();
    return c;
}

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
    }
    return condition;
}

bool testCreateSoftwareCanvas()
{
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(32, 24);
    bool ok = expect(canvas != nullptr, "createSoftware should return a canvas");
    if (!canvas) {
        return false;
    }
    ok = expect(canvas->getWidth() == 32 && canvas->getHeight() == 24, "software canvas should keep its size") && ok;

    std::vector<unsigned char> pixels;
    ok = expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA should succeed") && ok;
    ok = expect(pixels.size() == 32u * 24u * 4u, "framebuffer should be width*height*4 bytes") && ok;
    return ok;
}

bool testSolidFillRasterizes()
{
    const int w = 64;
    const int h = 64;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 0, 0, 255));
    fill.setAntiAlias(false); // crisp edges so we can assert exact colors
    canvas->drawRect(RectF(16.0f, 16.0f, 32.0f, 32.0f), fill);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    bool ok = expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA should succeed");
    ok = expect(pixels.size() == static_cast<std::size_t>(w) * h * 4u, "framebuffer size") && ok;
    if (!ok) {
        return false;
    }

    auto pixelAt = [&](int x, int y) -> const unsigned char * {
        return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u];
    };

    const unsigned char *center = pixelAt(32, 32);
    ok = expect(center[0] == 255 && center[1] == 0 && center[2] == 0 && center[3] == 255,
                "rect interior should be opaque red") && ok;

    const unsigned char *corner = pixelAt(2, 2);
    ok = expect(corner[3] == 0, "outside the rect should be transparent (cleared)") && ok;

    const Canvas::RenderStats stats = canvas->getRenderStats();
    ok = expect(stats.pathInputVertexCount == 4,
                "software path stats should count rectangle inputs") && ok;
    ok = expect(stats.pathTessellatedVertexCount == 6
                    && stats.pathAaExpandedVertexCount == 0,
                "software path stats should expose non-AA tessellation") && ok;
    ok = expect(stats.pathMergedVertexCount == 6
                    && stats.pathUploadedVertexCount == 6,
                "software path stats should count rasterizer-bound vertices") && ok;
    ok = expect(stats.commandObjectCount == 1
                    && stats.commandAllocationCount
                        + stats.commandPoolReuseCount == 1
                    && stats.commandCloneCount == 0,
                "software stats should count submitted command allocations") && ok;
    ok = expect(stats.stagingCapacityBytes > 0,
                "software stats should expose command staging capacity") && ok;

    canvas->beginFrame();
    canvas->drawRect(
        RectF(16.0f, 16.0f, 32.0f, 32.0f), fill);
    canvas->endFrame();
    const Canvas::RenderStats reused =
        canvas->getRenderStats();
    ok = expect(reused.commandObjectCount == 1
                    && reused.commandAllocationCount == 0
                    && reused.commandPoolReuseCount == 1,
                "warm-frame path commands should come from the reuse pool") && ok;

    return ok;
}

bool testSrcOverBlending()
{
    const int w = 32;
    const int h = 32;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    // Opaque blue background, then 50% red on top -> SrcOver should mix.
    Paint bg;
    bg.setStyle(Paint::Style::FILL);
    bg.setColor(Color(0, 0, 255, 255));
    bg.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), bg);

    Paint overlay;
    overlay.setStyle(Paint::Style::FILL);
    overlay.setColor(Color(255, 0, 0, 128));
    overlay.setAntiAlias(false);
    canvas->drawRect(RectF(8.0f, 8.0f, 16.0f, 16.0f), overlay);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }

    const unsigned char *mixed = &pixels[(16u * w + 16u) * 4u];
    // out = src*srcA + dst*(1-srcA); srcA=128/255~0.502. red ~128, blue ~127.
    bool ok = expect(mixed[0] > 120 && mixed[0] < 140, "blended red channel ~128");
    ok = expect(mixed[2] > 118 && mixed[2] < 138, "blended blue channel ~127") && ok;
    ok = expect(mixed[3] == 255, "blended alpha stays opaque") && ok;
    return ok;
}

bool testLinearGradient()
{
    const int w = 64;
    const int h = 16;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    Paint grad;
    grad.setStyle(Paint::Style::FILL);
    grad.setAntiAlias(false);
    grad.setLinearGradient(0.0f, 0.0f, static_cast<float>(w), 0.0f,
                           {Paint::ColorStop(0.0f, Color(255, 0, 0, 255)),
                            Paint::ColorStop(1.0f, Color(0, 0, 255, 255))});
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), grad);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    const unsigned char *left = pixelAt(1, 8);
    const unsigned char *right = pixelAt(62, 8);
    bool ok = expect(left[0] > 220 && left[2] < 40, "gradient left edge should be mostly red");
    ok = expect(right[2] > 220 && right[0] < 40, "gradient right edge should be mostly blue") && ok;
    const unsigned char *mid = pixelAt(32, 8);
    ok = expect(mid[0] > 80 && mid[0] < 200 && mid[2] > 80 && mid[2] < 200, "gradient midpoint should be mixed") && ok;
    return ok;
}

bool testDrawLine()
{
    const int w = 32;
    const int h = 32;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    Paint stroke;
    stroke.setStyle(Paint::Style::STROKE);
    stroke.setStrokeWidth(4.0f);
    stroke.setAntiAlias(false);
    stroke.setStrokeColor(Color(0, 255, 0, 255));
    canvas->drawLine(0.0f, 16.0f, 32.0f, 16.0f, stroke);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    const unsigned char *onLine = pixelAt(16, 16);
    bool ok = expect(onLine[1] == 255 && onLine[3] == 255, "pixel on the line should be opaque green");
    const unsigned char *offLine = pixelAt(16, 2);
    ok = expect(offLine[3] == 0, "pixel far from the line should be transparent") && ok;
    return ok;
}

bool testDrawImage()
{
    const int w = 16;
    const int h = 16;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    // A 4x4 opaque green image.
    std::vector<unsigned char> src(4 * 4 * 4, 0);
    for (std::size_t i = 0; i < 4u * 4u; ++i) {
        src[i * 4 + 0] = 0;
        src[i * 4 + 1] = 255;
        src[i * 4 + 2] = 0;
        src[i * 4 + 3] = 255;
    }
    Image image;
    bool ok = expect(image.loadFromRGBA(*canvas, src, 4, 4, false), "loadFromRGBA should succeed");
    if (!ok) {
        return false;
    }

    canvas->beginFrame();
    Paint paint;
    paint.setColor(Color(255, 255, 255, 255)); // white tint = show the image unchanged
    canvas->drawImage(image, 4.0f, 4.0f, paint);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    const unsigned char *inside = pixelAt(6, 6);
    ok = expect(inside[1] > 240 && inside[0] < 20 && inside[3] > 240, "image interior should be opaque green") && ok;
    const unsigned char *outside = pixelAt(0, 0);
    ok = expect(outside[3] == 0, "outside the image should be transparent") && ok;
    return ok;
}

bool testDrawImageTiledRepeat()
{
    constexpr int w = 16;
    constexpr int h = 8;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    // Two horizontal texels make wrap failures obvious: CLAMP stretches the
    // blue edge, while REPEAT alternates red/blue through the whole target.
    const std::vector<unsigned char> src = {
        255, 0, 0, 255, 0, 0, 255, 255,
    };
    Image image;
    if (!expect(image.loadFromRGBA(*canvas, src, 2, 1, false),
                "load tiled image from RGBA")) {
        return false;
    }

    Paint paint;
    paint.setColor(Color::WHITE);
    paint.setImageSampling(Paint::ImageSampling::NEAREST);
    paint.setImageTileMode(Paint::ImageTileMode::REPEAT);
    canvas->beginFrame();
    canvas->drawImageTiled(
        image, RectF(0.0f, 0.0f, static_cast<float>(w),
                     static_cast<float>(h)),
        4.0f, 2.0f, paint);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels)
        || pixels.size() != static_cast<std::size_t>(w * h * 4)) {
        return expect(false, "read tiled pixels");
    }
    const auto pixelAt = [&](int x, int y) {
        return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u];
    };
    const auto isRed = [&](int x) {
        const unsigned char *pixel = pixelAt(x, 3);
        return pixel[0] > 240 && pixel[2] < 15 && pixel[3] == 255;
    };
    const auto isBlue = [&](int x) {
        const unsigned char *pixel = pixelAt(x, 3);
        return pixel[0] < 15 && pixel[2] > 240 && pixel[3] == 255;
    };
    return expect(isRed(0) && isBlue(3) && isRed(4) && isBlue(7)
                      && isRed(8) && isBlue(11),
                  "REPEAT should preserve every tiled red/blue period");
}

bool testClipRect()
{
    const int w = 32;
    const int h = 32;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    canvas->save();
    canvas->clipRect(RectF(8.0f, 8.0f, 16.0f, 16.0f));
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 0, 0, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), fill);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    bool ok = expect(pixelAt(16, 16)[0] == 255 && pixelAt(16, 16)[3] == 255, "inside clipRect should be red");
    ok = expect(pixelAt(2, 2)[3] == 0, "outside clipRect should be untouched (transparent)") && ok;
    ok = expect(pixelAt(28, 28)[3] == 0, "past the clipRect should be untouched") && ok;
    return ok;
}

bool testClipPath()
{
    const int w = 40;
    const int h = 40;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    Path circle;
    circle.addCircle(20.0f, 20.0f, 12.0f);
    canvas->save();
    canvas->clipPath(circle);
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(0, 128, 255, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), fill);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    bool ok = expect(pixelAt(20, 20)[2] == 255 && pixelAt(20, 20)[3] == 255, "circle center should be filled");
    ok = expect(pixelAt(2, 2)[3] == 0, "corner outside the circle clip should be transparent") && ok;
    return ok;
}

bool testGaussianShadow()
{
    const int w = 64;
    const int h = 64;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    Paint p;
    p.setStyle(Paint::Style::FILL);
    p.setFillColor(Color(255, 255, 255, 255));
    p.setAntiAlias(false);
    p.setShadowLayer(6.0f, 8.0f, 8.0f, Color(0, 0, 0, 200));
    canvas->drawRect(RectF(16.0f, 16.0f, 20.0f, 20.0f), p);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    bool ok = expect(pixelAt(26, 26)[0] == 255 && pixelAt(26, 26)[3] == 255, "the shape itself should be opaque white");

    // The shadow is offset by (8,8) and blurred, so the region past the shape's
    // bottom-right corner should contain soft, dark, semi-transparent pixels.
    bool foundShadow = false;
    for (int y = 37; y < 48 && !foundShadow; ++y) {
        for (int x = 37; x < 48 && !foundShadow; ++x) {
            const unsigned char *px = pixelAt(x, y);
            if (px[3] > 10 && px[3] < 250 && px[0] < 90 && px[1] < 90 && px[2] < 90) {
                foundShadow = true;
            }
        }
    }
    ok = expect(foundShadow, "there should be a soft dark Gaussian shadow offset from the shape") && ok;
    return ok;
}

bool testSaveLayerAlpha()
{
    const int w = 32;
    const int h = 32;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255)); // white so the layer isn't tinted
    layerPaint.setAlpha(128);                        // composite the whole layer at ~50%
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), layerPaint);
    Paint red;
    red.setStyle(Paint::Style::FILL);
    red.setColor(Color(255, 0, 0, 255));
    red.setAntiAlias(false);
    canvas->drawRect(RectF(8.0f, 8.0f, 16.0f, 16.0f), red);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    // The whole layer is composited at ~50% alpha (128). The interior pixel sits
    // exactly on the shared diagonal of the layer image quad's two triangles, so
    // the top-left fill rule must ensure it is composited exactly once.
    const unsigned char *inside = pixelAt(16, 16);
    bool ok = expect(inside[0] > 100 && inside[1] < 20 && inside[2] < 20, "layer content should be red");
    ok = expect(inside[3] >= 118 && inside[3] <= 138, "layer should be composited at ~50% alpha (single pass)") && ok;
    ok = expect(pixelAt(2, 2)[3] == 0, "outside the drawn content should stay transparent") && ok;
    return ok;
}

// A layer smaller than the canvas renders into an offscreen target that is
// offset from the canvas origin. The layer's bounds-clip (an axis-aligned
// scissor in canvas space) must be translated into that target so content is
// clipped to the layer rectangle and nothing leaks outside it.
bool testSaveLayerPartial()
{
    const int w = 32;
    const int h = 32;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255));
    layerPaint.setAlpha(128);
    canvas->saveLayer(RectF(8.0f, 8.0f, 16.0f, 16.0f), layerPaint);
    Paint red;
    red.setStyle(Paint::Style::FILL);
    red.setColor(Color(255, 0, 0, 255));
    red.setAntiAlias(false);
    // Fill the whole canvas; only the part inside the layer bounds must survive.
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), red);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    const unsigned char *inside = pixelAt(16, 16);
    bool ok = expect(inside[0] > 100 && inside[1] < 20 && inside[2] < 20, "inside the layer should be red");
    ok = expect(inside[3] >= 118 && inside[3] <= 138, "inside the layer should be ~50% alpha") && ok;
    // Just outside the layer rect (x<8) must be clipped away entirely.
    ok = expect(pixelAt(4, 16)[3] == 0, "content left of the layer bounds should be clipped") && ok;
    ok = expect(pixelAt(28, 16)[3] == 0, "content right of the layer bounds should be clipped") && ok;
    return ok;
}

bool testBackdropBlur()
{
    const int w = 40;
    const int h = 24;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    Paint black;
    black.setStyle(Paint::Style::FILL);
    black.setColor(Color(0, 0, 0, 255));
    black.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, 20.0f, static_cast<float>(h)), black);

    Paint white = black;
    white.setColor(Color(255, 255, 255, 255));
    canvas->drawRect(RectF(20.0f, 0.0f, 20.0f, static_cast<float>(h)), white);

    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255));
    LayerOptions options;
    options.setBackdropFilter(ImageFilter::blur(6.0f));
    canvas->saveLayer(RectF(8.0f, 4.0f, 24.0f, 16.0f), layerPaint, options);
    // An empty layer is intentional: the filtered backdrop itself must still
    // be composited when restore closes the layer.
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "backdrop readPixelsRGBA should succeed");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    bool ok = true;
    const unsigned char *leftOutside = pixelAt(6, 12);
    const unsigned char *rightOutside = pixelAt(34, 12);
    ok = expect(leftOutside[0] == 0, "outside the backdrop layer the black half should remain sharp") && ok;
    ok = expect(rightOutside[0] == 255, "outside the backdrop layer the white half should remain sharp") && ok;

    const unsigned char *leftNearEdge = pixelAt(18, 12);
    const unsigned char *rightNearEdge = pixelAt(21, 12);
    ok = expect(leftNearEdge[0] > 0 && leftNearEdge[0] < 128,
                "backdrop blur should spread white into the black side") && ok;
    ok = expect(rightNearEdge[0] > 128 && rightNearEdge[0] < 255,
                "backdrop blur should spread black into the white side") && ok;
    ok = expect(leftNearEdge[3] == 255 && rightNearEdge[3] == 255,
                "an opaque backdrop should remain opaque after blur") && ok;
    return ok;
}

bool testBackdropColorAdjustment()
{
    const int w = 20;
    const int h = 20;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    Paint background;
    background.setStyle(Paint::Style::FILL);
    background.setColor(Color(240, 80, 40, 255));
    background.setAntiAlias(false);
    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255));
    ImageFilter grayscale = ImageFilter::blur(1.0f);
    grayscale.setColorAdjustment(0.0f);
    LayerOptions options;
    options.setBackdropFilter(grayscale);

    canvas->beginFrame();
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), background);
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)),
                      layerPaint, options);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "color-adjusted backdrop readPixelsRGBA should succeed");
    }
    const unsigned char *center =
        &pixels[(static_cast<std::size_t>(h / 2) * w + w / 2) * 4u];
    const int rg = std::abs(static_cast<int>(center[0]) - static_cast<int>(center[1]));
    const int gb = std::abs(static_cast<int>(center[1]) - static_cast<int>(center[2]));
    bool ok = expect(rg <= 2 && gb <= 2,
                     "zero saturation should turn the filtered backdrop grayscale");
    ok = expect(center[3] == 255, "color adjustment should preserve backdrop alpha") && ok;
    const Canvas::RenderStats stats = canvas->getRenderStats();
    ok = expect(stats.filterCount == 1 && stats.filterPassCount == 3
                    && stats.downsampledFilterCount == 0,
                "software stats should report blur plus color-adjustment passes") && ok;
    ok = expect(stats.filterInputPixelCount == 400
                    && stats.filterPixelPassCount == 1200,
                "software stats should report full-resolution pixel-pass work") && ok;
    return ok;
}

bool testBackdropSamplingOutsetIsClipped()
{
    const int w = 40;
    const int h = 24;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    Paint black;
    black.setStyle(Paint::Style::FILL);
    black.setColor(Color(0, 0, 0, 255));
    black.setAntiAlias(false);
    Paint white = black;
    white.setColor(Color(255, 255, 255, 255));
    Paint layerPaint = white;
    LayerOptions options;
    options.setBackdropFilter(ImageFilter::blur(6.0f));

    canvas->beginFrame();
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), black);
    canvas->drawRect(RectF(6.0f, 0.0f, 2.0f, static_cast<float>(h)), white);
    canvas->saveLayer(RectF(8.0f, 4.0f, 24.0f, 16.0f), layerPaint, options);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels)
        || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "bounded backdrop readPixelsRGBA should succeed");
    }
    auto redAt = [&](int x, int y) {
        return pixels[(static_cast<std::size_t>(y) * w + x) * 4u];
    };

    bool ok = expect(redAt(5, 12) == 0,
                     "sampling outset must not change black pixels outside the layer");
    ok = expect(redAt(7, 12) == 255,
                "sampling outset must not overwrite source pixels outside the layer") && ok;
    ok = expect(redAt(9, 12) > 0,
                "source pixels outside the layer should contribute to blur inside it") && ok;
    return ok;
}

bool testLayerImageBlur()
{
    const int w = 32;
    const int h = 24;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255));
    LayerOptions options;
    options.setImageFilter(ImageFilter::blur(5.0f));
    Paint red;
    red.setStyle(Paint::Style::FILL);
    red.setColor(Color(255, 0, 0, 255));
    red.setAntiAlias(false);

    canvas->beginFrame();
    canvas->saveLayer(RectF(4.0f, 4.0f, 24.0f, 16.0f), layerPaint, options);
    canvas->drawRect(RectF(14.0f, 8.0f, 4.0f, 8.0f), red);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "image-filter readPixelsRGBA should succeed");
    }
    auto alphaAt = [&](int x, int y) {
        return pixels[(static_cast<std::size_t>(y) * w + x) * 4u + 3u];
    };

    bool ok = expect(alphaAt(16, 12) > 80, "blurred layer content should remain visible at its center");
    ok = expect(alphaAt(12, 12) > 0, "image blur should spread alpha outside the source rect") && ok;
    ok = expect(alphaAt(2, 12) == 0, "image blur should remain clipped to the saved layer") && ok;
    return ok;
}

bool testLayerInnerShadow()
{
    constexpr int w = 48;
    constexpr int h = 36;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    const RectF bounds(8.0f, 6.0f, 32.0f, 24.0f);
    Paint layerPaint;
    layerPaint.setColor(Color::WHITE);
    LayerOptions options;
    options.setImageFilter(ImageFilter::innerShadow(
        6.0f, 4.0f, 4.0f, Color(0, 0, 0, 220)));
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(240, 240, 240, 255));
    fill.setAntiAlias(false);

    canvas->beginFrame();
    canvas->saveLayer(bounds, layerPaint, options);
    canvas->drawRect(bounds, fill);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels)
        || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "inner-shadow readPixelsRGBA should succeed");
    }
    auto pixelAt = [&](int x, int y) {
        return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u];
    };

    const unsigned char *topLeft = pixelAt(9, 7);
    const unsigned char *center = pixelAt(24, 18);
    const unsigned char *bottomRight = pixelAt(38, 28);
    bool ok = expect(topLeft[0] < 150 && topLeft[1] < 150 && topLeft[2] < 150,
                     "positive inset offsets should shade the top-left edge");
    ok = expect(center[0] > 225 && center[1] > 225 && center[2] > 225,
                "inner shadow should preserve the center fill") && ok;
    ok = expect(bottomRight[0] > topLeft[0] + 60,
                "the edge opposite the inset offset should remain brighter") && ok;
    ok = expect(topLeft[3] == 255 && center[3] == 255,
                "inner shadow should preserve source alpha") && ok;
    ok = expect(pixelAt(7, 18)[3] == 0 && pixelAt(40, 18)[3] == 0,
                "inner shadow must not leak outside layer bounds") && ok;
    const Canvas::RenderStats stats = canvas->getRenderStats();
    ok = expect(stats.filterCount == 1 && stats.filterPassCount == 3,
                "inner shadow should report two blur passes plus composite") && ok;
    return ok;
}

bool testFullBleedInnerShadow()
{
    constexpr int w = 64;
    constexpr int h = 48;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }
    Paint layerPaint;
    layerPaint.setColor(Color::WHITE);
    LayerOptions options;
    options.setImageFilter(ImageFilter::innerShadow(
        8.0f, 5.0f, 4.0f, Color(0, 0, 0, 220)));
    Paint fill;
    fill.setColor(Color::WHITE);
    fill.setAntiAlias(false);

    canvas->beginFrame();
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w),
                            static_cast<float>(h)),
                      layerPaint, options);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w),
                           static_cast<float>(h)),
                     fill);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels)) {
        return expect(false, "full-bleed inner-shadow readback should succeed");
    }
    auto pixelAt = [&](int x, int y) {
        return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u];
    };
    return expect(pixelAt(0, 0)[0] < 170,
                  "transparent-outside sampling should shade a full-bleed edge")
        && expect(pixelAt(w / 2, h / 2)[0] > 245,
                  "full-bleed inner shadow should preserve its center");
}

bool testComposableImageFilterChain()
{
    constexpr int w = 32;
    constexpr int h = 32;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    const std::array<float, 20> redToGreen = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    };
    ImageFilterChain filters;
    filters.appendColorMatrix(redToGreen).appendOffset(4.0f, 3.0f);
    LayerOptions options;
    options.setImageFilter(filters);
    Paint layerPaint;
    layerPaint.setColor(Color::WHITE);
    Paint red;
    red.setColor(Color(255, 0, 0, 255));
    red.setAntiAlias(false);

    canvas->beginFrame();
    canvas->saveLayer(
        RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)),
        layerPaint, options);
    canvas->drawRect(RectF(8.0f, 8.0f, 8.0f, 8.0f), red);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels)) {
        return expect(false, "composable filter readback should succeed");
    }
    auto pixelAt = [&](int x, int y) {
        return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u];
    };
    const unsigned char *shifted = pixelAt(14, 14);
    const unsigned char *oldPosition = pixelAt(9, 9);
    const Canvas::RenderStats stats = canvas->getRenderStats();
    return expect(
               shifted[0] == 0 && shifted[1] == 255
                   && shifted[2] == 0 && shifted[3] == 255,
               "color matrix should run before the offset node")
        && expect(oldPosition[3] == 0,
                  "offset should expose transparent pixels at the old position")
        && expect(stats.filterCount == 2 && stats.filterPassCount == 2,
                  "generic chain nodes should be visible in filter diagnostics")
        && expect(stats.flushCpuTimeNs > 0
                      && stats.deviceExecutionCpuTimeNs > 0,
                  "Software should report frame CPU timing");
}

// With gamma-correct rendering the software backend blends in linear space and
// re-encodes to sRGB (mirroring the GL backend's linearized source color +
// GL_FRAMEBUFFER_SRGB). A 50% red over opaque blue must land near the linear
// result (R,B ~= 188/187) rather than the straight-sRGB result (~128/127).
bool testGammaLinearBlend()
{
    const int w = 16;
    const int h = 16;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    Canvas::setGammaCorrect(true);
    canvas->beginFrame();
    Paint bg;
    bg.setStyle(Paint::Style::FILL);
    bg.setAntiAlias(false);
    bg.setColor(Color(0, 0, 255, 255));
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), bg);

    Paint over;
    over.setStyle(Paint::Style::FILL);
    over.setAntiAlias(false);
    over.setColor(Color(255, 0, 0, 128));
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), over);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    const bool read = canvas->readPixelsRGBA(pixels) && pixels.size() == static_cast<std::size_t>(w) * h * 4u;
    Canvas::setGammaCorrect(false); // restore global state for later tests
    if (!read) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }

    const unsigned char *mid = &pixels[(8u * w + 8u) * 4u];
    bool ok = expect(mid[0] > 178 && mid[0] < 198, "gamma linear blend red channel ~188");
    ok = expect(mid[2] > 177 && mid[2] < 197, "gamma linear blend blue channel ~187") && ok;
    ok = expect(mid[1] < 12, "gamma linear blend green channel ~0") && ok;
    ok = expect(mid[3] == 255, "gamma blend alpha stays opaque") && ok;
    // The linear blend must be clearly brighter than a straight-sRGB blend (~128).
    ok = expect(mid[0] > 160, "gamma blend should differ from straight-sRGB blend") && ok;
    return ok;
}

bool testEvenOddFillPreservesHole()
{
    constexpr int w = 48;
    constexpr int h = 48;
    std::unique_ptr<Canvas> canvas = makeSoftwareCanvas(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    Paint background;
    background.setColor(Color(10, 20, 40, 255));
    background.setAntiAlias(false);
    Paint fill;
    fill.setColor(Color(240, 80, 110, 255));
    fill.setAntiAlias(false);
    Path ring;
    ring.setFillType(Path::FillType::EVEN_ODD);
    ring.addRect(RectF(4.0f, 4.0f, 40.0f, 40.0f));
    ring.addRect(RectF(16.0f, 16.0f, 16.0f, 16.0f));

    canvas->beginFrame();
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w),
                           static_cast<float>(h)), background);
    canvas->drawPath(ring, fill);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels)) {
        return expect(false, "even-odd readback should succeed");
    }
    auto pixelAt = [&](int x, int y) {
        return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u];
    };
    const unsigned char *ringPixel = pixelAt(9, 24);
    const unsigned char *holePixel = pixelAt(24, 24);
    return expect(ringPixel[0] == 240 && ringPixel[1] == 80
                      && ringPixel[2] == 110 && ringPixel[3] == 255,
                  "even-odd outer contour should remain filled")
        && expect(holePixel[0] == 10 && holePixel[1] == 20
                      && holePixel[2] == 40 && holePixel[3] == 255,
                  "even-odd inner contour should remain a hole");
}

} // namespace

int main()
{
    bool ok = testCreateSoftwareCanvas();
    ok = testSolidFillRasterizes() && ok;
    ok = testSrcOverBlending() && ok;
    ok = testLinearGradient() && ok;
    ok = testDrawLine() && ok;
    ok = testDrawImage() && ok;
    ok = testDrawImageTiledRepeat() && ok;
    ok = testClipRect() && ok;
    ok = testClipPath() && ok;
    ok = testGaussianShadow() && ok;
    ok = testSaveLayerAlpha() && ok;
    ok = testSaveLayerPartial() && ok;
    ok = testBackdropBlur() && ok;
    ok = testBackdropColorAdjustment() && ok;
    ok = testBackdropSamplingOutsetIsClipped() && ok;
    ok = testLayerImageBlur() && ok;
    ok = testLayerInnerShadow() && ok;
    ok = testFullBleedInnerShadow() && ok;
    ok = testComposableImageFilterChain() && ok;
    ok = testEvenOddFillPreservesHole() && ok;
    ok = testGammaLinearBlend() && ok;
    return ok ? 0 : 1;
}
