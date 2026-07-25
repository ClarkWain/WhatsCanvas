#pragma once

#include <cstddef>
#include <vector>

#include "wsc/wsc.h"

namespace whatscanvas::test {

constexpr int kCompositeParityWidth = 192;
constexpr int kCompositeParityHeight = 128;

inline std::vector<unsigned char> makeParityImage()
{
    constexpr int size = 16;
    std::vector<unsigned char> pixels(size * size * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * size + x) * 4u;
            const bool alternate = ((x / 4) + (y / 4)) % 2 != 0;
            pixels[index + 0] = alternate ? 242 : 34;
            pixels[index + 1] = alternate ? 92 : 184;
            pixels[index + 2] = alternate ? 54 : 232;
            pixels[index + 3] = 255;
        }
    }
    return pixels;
}

inline bool drawCompositeFilterParityScene(wsc::Canvas &canvas)
{
    wsc::Image image;
    const std::vector<unsigned char> imagePixels = makeParityImage();
    if (!canvas.loadImageFromRGBA(image, imagePixels, 16, 16)) {
        return false;
    }

    canvas.beginFrame();

    wsc::Paint background;
    background.setAntiAlias(false);
    background.setLinearGradient(
        0.0f, 0.0f, static_cast<float>(kCompositeParityWidth), 0.0f,
        wsc::Color(18, 34, 62, 255), wsc::Color(38, 104, 126, 255));
    canvas.drawRect(
        wsc::RectF(0.0f, 0.0f, static_cast<float>(kCompositeParityWidth),
                   static_cast<float>(kCompositeParityHeight)),
        background);

    wsc::Paint stripe;
    stripe.setAntiAlias(false);
    for (int x = 0; x < kCompositeParityWidth; x += 16) {
        stripe.setColor((x / 16) % 2 == 0
                            ? wsc::Color(245, 132, 74, 210)
                            : wsc::Color(65, 192, 210, 190));
        canvas.drawRect(wsc::RectF(static_cast<float>(x), 12.0f, 8.0f, 104.0f),
                        stripe);
    }

    wsc::Paint imagePaint;
    imagePaint.setColor(wsc::Color(255, 255, 255, 255));
    imagePaint.setAntiAlias(false);
    canvas.drawImage(image, wsc::RectF(66.0f, 8.0f, 58.0f, 48.0f), imagePaint);

    wsc::Paint composite;
    composite.setColor(wsc::Color(255, 255, 255, 255));
    composite.setAntiAlias(false);
    wsc::Paint glassTint;
    glassTint.setColor(wsc::Color(230, 244, 255, 74));
    glassTint.setAntiAlias(false);

    wsc::LayerOptions glassA;
    glassA.setBackdropFilter(
        wsc::ImageFilter::frostedGlass(3.0f, 1.08f, 1.02f, 1.0f, 0.0f));
    canvas.saveLayer(wsc::RectF(24.0f, 28.0f, 94.0f, 54.0f), composite, glassA);
    canvas.drawRect(wsc::RectF(24.0f, 28.0f, 94.0f, 54.0f), glassTint);
    canvas.restore();

    wsc::LayerOptions glassB;
    glassB.setBackdropFilter(
        wsc::ImageFilter::frostedGlass(2.0f, 0.94f, 1.04f, 1.0f, 0.0f));
    canvas.saveLayer(wsc::RectF(76.0f, 52.0f, 92.0f, 50.0f), composite, glassB);
    glassTint.setColor(wsc::Color(214, 232, 255, 62));
    canvas.drawRect(wsc::RectF(76.0f, 52.0f, 92.0f, 50.0f), glassTint);
    canvas.restore();

    wsc::LayerOptions inset;
    inset.setImageFilter(wsc::ImageFilter::innerShadow(
        5.0f, 4.0f, 2.0f, 2.0f, wsc::Color(4, 14, 26, 180)));
    canvas.saveLayer(wsc::RectF(42.0f, 88.0f, 66.0f, 26.0f), composite, inset);
    wsc::Paint control;
    control.setAntiAlias(false);
    control.setColor(wsc::Color(228, 239, 246, 236));
    canvas.drawRect(wsc::RectF(42.0f, 88.0f, 66.0f, 26.0f), control);
    canvas.restore();

    canvas.save();
    canvas.clipRect(wsc::RectF(126.0f, 16.0f, 50.0f, 30.0f));
    wsc::Paint clippedGradient;
    clippedGradient.setAntiAlias(false);
    clippedGradient.setLinearGradient(
        126.0f, 16.0f, 176.0f, 46.0f,
        wsc::Color(248, 212, 88, 255), wsc::Color(122, 78, 224, 255));
    canvas.drawRect(wsc::RectF(122.0f, 12.0f, 58.0f, 38.0f), clippedGradient);
    canvas.restore();

    canvas.endFrame();
    return true;
}

inline bool renderCompositeFilterParityScene(
    wsc::Canvas::Backend backend, std::vector<unsigned char> &pixels,
    wsc::Canvas::RenderStats *stats = nullptr)
{
    auto canvas = wsc::Canvas::create(
        backend, kCompositeParityWidth, kCompositeParityHeight);
    if (!canvas || !canvas->initializeContext()
        || !drawCompositeFilterParityScene(*canvas)) {
        return false;
    }
    if (stats != nullptr) {
        *stats = canvas->getRenderStats();
    }
    return canvas->readPixelsRGBA(pixels);
}

} // namespace whatscanvas::test
