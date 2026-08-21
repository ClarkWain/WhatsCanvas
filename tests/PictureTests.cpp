#include <wsc/CanvasStats.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

#include "wsc/wsc.h"

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "[PictureTests] FAIL: " << message << '\n';
    }
    return condition;
}

bool sameMatrix(const wsc::Matrix4 &lhs, const wsc::Matrix4 &rhs)
{
    const auto &a = lhs.values();
    const auto &b = rhs.values();
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i] - b[i]) > 1e-6f) {
            return false;
        }
    }
    return true;
}

void drawRetainedScene(wsc::Canvas &canvas)
{
    wsc::Paint background;
    background.setStyle(wsc::Paint::Style::FILL);
    background.setFillColor(wsc::Color(8, 13, 28));
    canvas.drawRect(wsc::RectF(0.0f, 0.0f, 64.0f, 64.0f), background);

    canvas.save();
    canvas.clipRect(wsc::RectF(7.0f, 8.0f, 48.0f, 45.0f));
    canvas.translate(3.0f, 2.0f);

    wsc::Paint fill;
    fill.setStyle(wsc::Paint::Style::FILL);
    fill.setFillColor(wsc::Color(53, 219, 191));
    canvas.drawRoundRect(wsc::RectF(8.0f, 9.0f, 39.0f, 27.0f), 6.0f, fill);

    wsc::Path triangle;
    triangle.moveTo(13.0f, 47.0f);
    triangle.lineTo(31.0f, 23.0f);
    triangle.lineTo(48.0f, 48.0f);
    triangle.close();
    fill.setFillColor(wsc::Color(117, 133, 249, 190));
    canvas.drawPath(triangle, fill);
    canvas.restore();
}

std::vector<unsigned char> renderDirect(wsc::Canvas &canvas)
{
    canvas.beginFrame();
    drawRetainedScene(canvas);
    canvas.endFrame();
    return canvas.readPixelsRGBA();
}

std::vector<unsigned char> renderPicture(
    wsc::Canvas &canvas, const wsc::Picture &picture)
{
    canvas.beginFrame();
    canvas.drawPicture(picture);
    canvas.endFrame();
    return canvas.readPixelsRGBA();
}

std::vector<unsigned char> renderRasterizedPicture(
    wsc::Canvas &canvas, const wsc::Picture &picture)
{
    canvas.beginFrame();
    canvas.drawPictureRasterized(picture);
    canvas.endFrame();
    return canvas.readPixelsRGBA();
}

} // namespace

int main()
{
    wsc::Canvas::initialize();
    auto recorder = wsc::Canvas::create(wsc::Canvas::Backend::Software, 64, 64);
    auto direct = wsc::Canvas::create(wsc::Canvas::Backend::Software, 64, 64);
    auto replay = wsc::Canvas::create(wsc::Canvas::Backend::Software, 64, 64);
    if (!expect(recorder && direct && replay, "software canvases should be available")) {
        wsc::Canvas::finalize();
        return 1;
    }

    const int saveCountBefore = recorder->getSaveCount();
    const wsc::Matrix4 matrixBefore = recorder->getMatrix();
    const auto picture = recorder->recordPicture(drawRetainedScene);
    bool ok = true;
    ok &= expect(picture != nullptr, "backend-neutral scene should record");
    ok &= expect(picture && picture->operationCount() == 7u,
                 "recorded stream should contain stable primitive/state operations");
    ok &= expect(recorder->getSaveCount() == saveCountBefore,
                 "recording should restore the Canvas save stack");
    ok &= expect(sameMatrix(recorder->getMatrix(), matrixBefore),
                 "recording should restore the Canvas transform");

    if (picture) {
        const auto expected = renderDirect(*direct);
        const auto actual = renderPicture(*replay, *picture);
        ok &= expect(!expected.empty() && actual == expected,
                     "Picture replay should be pixel-identical to direct drawing");

        const auto ownerFirst = renderPicture(*recorder, *picture);
        const auto firstStats = recorder->getRenderStats();
        const auto ownerSecond = renderPicture(*recorder, *picture);
        const auto secondStats = recorder->getRenderStats();
        ok &= expect(ownerFirst == expected && ownerSecond == expected,
                     "compiled Picture replay should preserve pixels");
        ok &= expect(firstStats.retainedPictureCacheMisses == 1u,
                     "first owner replay should compile the Picture");
        ok &= expect(secondStats.retainedPictureCacheHits == 1u,
                     "second owner replay should reuse compiled commands");
        ok &= expect(firstStats.commandCloneCount > 0u
                         && secondStats.commandCloneCount > 0u,
                     "Picture compilation and cache replay should report command clones");

        const auto rasterFirst = renderRasterizedPicture(*recorder, *picture);
        const auto rasterFirstStats = recorder->getRenderStats();
        const auto rasterSecond = renderRasterizedPicture(*recorder, *picture);
        const auto rasterSecondStats = recorder->getRenderStats();
        ok &= expect(rasterFirst == expected && rasterSecond == expected,
                     "rasterized Picture replay should preserve exact pixels");
        ok &= expect(rasterFirstStats.retainedPictureRasterCacheMisses == 1u,
                     "first rasterized replay should populate the raster cache");
        ok &= expect(rasterSecondStats.retainedPictureRasterCacheHits == 1u,
                     "second rasterized replay should reuse the cached layer");
        ok &= expect(
            rasterSecondStats.retainedPictureRasterCacheSize == 1u
                && rasterSecondStats.retainedPictureRasterCacheBytes
                    == 64u * 64u * 4u
                && rasterSecondStats.retainedPictureRasterCacheEvictions == 0u,
            "raster cache should report its bounded resident layer memory");

        replay->translate(4.0f, 5.0f);
        const auto playbackMatrix = replay->getMatrix();
        const int playbackSaveCount = replay->getSaveCount();
        replay->beginFrame();
        replay->drawPicture(*picture);
        replay->endFrame();
        ok &= expect(sameMatrix(replay->getMatrix(), playbackMatrix),
                     "Picture playback should not leak transform state");
        ok &= expect(replay->getSaveCount() == playbackSaveCount,
                     "Picture playback should not leak save state");

        replay->finalizeContext();
        ok &= expect(replay->initializeContext(),
                     "software context should reinitialize");
        const auto afterRecreate = renderPicture(*replay, *picture);
        ok &= expect(!afterRecreate.empty(),
                     "backend-neutral Picture should replay after context recreation");

        recorder->finalizeContext();
        ok &= expect(recorder->initializeContext(),
                     "owner context should reinitialize");
        const auto ownerAfterRecreate = renderPicture(*recorder, *picture);
        const auto recreateStats = recorder->getRenderStats();
        ok &= expect(ownerAfterRecreate == expected,
                     "compiled Picture should preserve pixels after owner context recreation");
        ok &= expect(recreateStats.retainedPictureCacheMisses == 1u,
                     "context recreation should invalidate compiled Picture commands");

        const auto rasterAfterRecreate =
            renderRasterizedPicture(*recorder, *picture);
        const auto rasterRecreateStats = recorder->getRenderStats();
        ok &= expect(rasterAfterRecreate == expected,
                     "rasterized Picture should preserve pixels after context recreation");
        ok &= expect(
            rasterRecreateStats.retainedPictureRasterCacheMisses == 1u,
            "context recreation should invalidate the Picture raster cache");
    }

    std::vector<unsigned char> redPixels(4u * 4u * 4u, 0u);
    std::vector<unsigned char> bluePixels(4u * 4u * 4u, 0u);
    for (std::size_t pixel = 0; pixel < 16u; ++pixel) {
        redPixels[pixel * 4u] = 255u;
        redPixels[pixel * 4u + 3u] = 255u;
        bluePixels[pixel * 4u + 2u] = 255u;
        bluePixels[pixel * 4u + 3u] = 255u;
    }
    wsc::Image snapshotSource;
    ok &= expect(snapshotSource.loadFromRGBA(
                     *recorder, redPixels, 4, 4),
                 "owned RGBA image should load before Picture recording");
    const auto imagePicture = recorder->recordPicture(
        [&](wsc::Canvas &canvas) {
            canvas.drawImage(
                snapshotSource, wsc::RectF(12.0f, 14.0f, 20.0f, 18.0f),
                wsc::Paint());
        });
    ok &= expect(imagePicture != nullptr,
                 "CPU-backed image should enter a backend-neutral Picture");
    ok &= expect(snapshotSource.replacePixelsRGBA(
                     *recorder, bluePixels, 4, 4),
                 "source image should remain mutable after recording");

    wsc::Image expectedRed;
    ok &= expect(expectedRed.loadFromRGBA(*direct, redPixels, 4, 4),
                 "reference image should load");
    direct->resetMatrix();
    replay->resetMatrix();
    direct->beginFrame();
    direct->drawImage(
        expectedRed, wsc::RectF(12.0f, 14.0f, 20.0f, 18.0f), wsc::Paint());
    direct->endFrame();
    const auto expectedImagePixels = direct->readPixelsRGBA();
    const auto actualImagePixels = renderPicture(
        *replay, *imagePicture);
    ok &= expect(actualImagePixels == expectedImagePixels,
                 "Picture image snapshot should be immutable after source mutation");

    wsc::Image mutableGpuImage;
    const auto rejected = recorder->recordPicture(
        [&](wsc::Canvas &canvas) {
            wsc::Paint paint;
            canvas.drawImage(mutableGpuImage, 0.0f, 0.0f, paint);
        });
    ok &= expect(rejected == nullptr,
                 "images without a CPU snapshot must not enter a backend-neutral Picture");

    replay.reset();
    direct.reset();
    recorder.reset();
    wsc::Canvas::finalize();
    if (!ok) {
        return 1;
    }
    std::cout << "[PictureTests] PASS\n";
    return 0;
}
