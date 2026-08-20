#include <jni.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <limits>
#include <string>
#include <vector>

#include <wsc/wsc.h>

#include "platforms/shared/scenes/CanonicalViewport.h"

namespace {

constexpr const char* kLogTag = "WhatsCanvas";

constexpr const char* kSansFamily = wsc::FontSystem::kDefaultPrimaryFamily;
constexpr const char* kCjkFamily = wsc::FontSystem::kDefaultCjkFamily;
constexpr const char* kComplexEmojiText =
    "\xE4\xB8\xAD\xE6\x96\x87 "
    "\xF0\x9F\x91\xA9\xF0\x9F\x8F\xBD\xE2\x80\x8D\xF0\x9F\x92\xBB "
    "\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3 8\xEF\xB8\x8F\xE2\x83\xA3";
constexpr float kPi = 3.14159265358979323846f;

void logError(const char* message)
{
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
}

int androidLogPriority(wsc::LogLevel level)
{
    switch (level) {
    case wsc::LogLevel::Trace:
    case wsc::LogLevel::Debug:
        return ANDROID_LOG_DEBUG;
    case wsc::LogLevel::Info:
        return ANDROID_LOG_INFO;
    case wsc::LogLevel::Warning:
        return ANDROID_LOG_WARN;
    case wsc::LogLevel::Error:
        return ANDROID_LOG_ERROR;
    case wsc::LogLevel::Off:
        return ANDROID_LOG_SILENT;
    }
    return ANDROID_LOG_DEFAULT;
}

void* loadOpenGlesProcedure(const char* name)
{
    const auto procedure = eglGetProcAddress(name);
    if (procedure != nullptr) {
        return reinterpret_cast<void*>(procedure);
    }
    return dlsym(RTLD_DEFAULT, name);
}

void createCheckerImage(wsc::Canvas& canvas,
                        std::unique_ptr<wsc::Image>& checkerImage)
{
    constexpr int imageSize = 24;
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(imageSize * imageSize * 4));
    for (int y = 0; y < imageSize; ++y) {
        for (int x = 0; x < imageSize; ++x) {
            const bool alternate = ((x / 6) + (y / 6)) % 2 != 0;
            const std::size_t offset = static_cast<std::size_t>(
                (y * imageSize + x) * 4);
            pixels[offset + 0] = alternate ? 78 : 32;
            pixels[offset + 1] = alternate ? 218 : 98;
            pixels[offset + 2] = alternate ? 206 : 230;
            pixels[offset + 3] = 255;
        }
    }

    checkerImage = std::make_unique<wsc::Image>();
    if (!canvas.loadImageFromRGBA(
            *checkerImage, pixels, imageSize, imageSize, true)) {
        logError("Checker image upload failed");
        checkerImage.reset();
    } else {
        __android_log_print(ANDROID_LOG_INFO, kLogTag,
                            "RGBA image upload ready: %dx%d", imageSize, imageSize);
    }
}

wsc::Paint makeTextPaint(float size, const wsc::Color& color,
                         const char* family = kSansFamily)
{
    wsc::Paint paint;
    paint.setStyle(wsc::Paint::Style::FILL);
    paint.setFillColor(color);
    paint.setTextSize(size);
    paint.setFontFamily(family);
    paint.setTextBaseline(wsc::Paint::TextBaseline::TOP);
    paint.setAntiAlias(true);
    return paint;
}

wsc::Path makeStar(float centerX, float centerY, float outerRadius,
                   float innerRadius, int points = 5)
{
    wsc::Path path;
    for (int i = 0; i < points * 2; ++i) {
        const float radius = (i % 2 == 0) ? outerRadius : innerRadius;
        const float angle = -kPi * 0.5f
            + static_cast<float>(i) * kPi / static_cast<float>(points);
        const float x = centerX + std::cos(angle) * radius;
        const float y = centerY + std::sin(angle) * radius;
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    path.close();
    return path;
}

void drawCard(wsc::Canvas& canvas, const wsc::RectF& rect,
              const std::string& label)
{
    wsc::Paint card;
    card.setStyle(wsc::Paint::Style::FILL);
    card.setFillColor(wsc::Color(19, 28, 53, 238));
    canvas.drawRoundRect(rect, 13.0f, card);

    wsc::Paint border;
    border.setStyle(wsc::Paint::Style::STROKE);
    border.setStrokeWidth(1.0f);
    border.setStrokeColor(wsc::Color(91, 115, 173, 105));
    border.setAntiAlias(true);
    canvas.drawRoundRect(rect, 13.0f, border);

    auto labelPaint = makeTextPaint(10.0f, wsc::Color(139, 166, 226));
    labelPaint.setLetterSpacing(0.7f);
    canvas.drawText(label, rect.getX() + 11.0f, rect.getY() + 9.0f, labelPaint);
}

void drawScene(wsc::Canvas& canvas, const wsc::Image* checkerImage,
               float logicalWidth, float logicalHeight,
               float elapsedSeconds)
{
    using wsc::Color;
    using wsc::Paint;
    using wsc::Path;
    using wsc::PointF;
    using wsc::RectF;

    const float width = logicalWidth;
    const float height = logicalHeight;
    const float shortSide = std::max(1.0f, std::min(width, height));
    const float pulse = 0.5f + 0.5f * std::sin(elapsedSeconds * 2.2f);

    Paint background;
    background.setStyle(Paint::Style::FILL);
    background.setLinearGradient(0.0f, 0.0f, width, height, {
        Paint::ColorStop(0.0f, Color(8, 12, 29)),
        Paint::ColorStop(0.52f, Color(22, 31, 62)),
        Paint::ColorStop(1.0f, Color(19, 12, 43))
    });
    canvas.drawRect(RectF(0.0f, 0.0f, width, height), background);

    Paint ambientGlow;
    ambientGlow.setStyle(Paint::Style::FILL);
    ambientGlow.setFillColor(Color(58, 212, 224, 22));
    canvas.drawCircle(PointF(width * 0.12f, height * 0.2f), shortSide * 0.38f, ambientGlow);
    ambientGlow.setFillColor(Color(150, 86, 232, 20));
    canvas.drawCircle(PointF(width * 0.92f, height * 0.78f), shortSide * 0.42f, ambientGlow);

    auto title = makeTextPaint(23.0f, Color(242, 247, 255));
    title.setFontWeight(400);
    canvas.drawText("WhatsCanvas Android", 15.0f, 13.0f, title);
    auto subtitle = makeTextPaint(10.5f, Color(130, 157, 211));
    canvas.drawText("GLES 3  |  API 33  |  live feature matrix", 16.0f, 44.0f, subtitle);

    Paint livePill;
    livePill.setStyle(Paint::Style::FILL);
    livePill.setFillColor(Color(43, 204, 164, 42));
    const RectF liveRect(width - 58.0f, 17.0f, 43.0f, 21.0f);
    canvas.drawRoundRect(liveRect, 10.5f, livePill);
    auto liveText = makeTextPaint(9.0f, Color(83, 235, 196));
    liveText.setTextAlign(Paint::TextAlign::CENTER);
    canvas.drawText("LIVE", liveRect.getX() + liveRect.getWidth() * 0.5f,
                    liveRect.getY() + 5.0f, liveText);

    const float margin = 14.0f;
    const float gapX = 8.0f;
    const float gapY = 8.0f;
    const float gridTop = 69.0f;
    const float gridBottom = height - 35.0f;
    const bool landscape = width > height;
    const int columns = landscape ? 4 : 2;
    const int rows = landscape ? 2 : 4;
    const float cardWidth =
        (width - margin * 2.0f - gapX * static_cast<float>(columns - 1))
        / static_cast<float>(columns);
    const float cardHeight =
        (gridBottom - gridTop - gapY * static_cast<float>(rows - 1))
        / static_cast<float>(rows);
    const bool compactCards = cardHeight < 145.0f;
    auto cardRect = [&](int index) {
        const int column = index % columns;
        const int row = index / columns;
        return RectF(margin + static_cast<float>(column) * (cardWidth + gapX),
                     gridTop + static_cast<float>(row) * (cardHeight + gapY),
                     cardWidth, cardHeight);
    };

    // 1. Portable glyph atlas, font fallback, gradients and text layout.
    const RectF textCard = cardRect(0);
    drawCard(canvas, textCard, "TEXT + UTF-8");
    auto heroText = makeTextPaint(compactCards ? 23.0f : 27.0f, Color::WHITE);
    heroText.setLinearGradient(textCard.getX() + 11.0f, 0.0f,
                               textCard.getX() + cardWidth - 12.0f, 0.0f, {
        Paint::ColorStop(0.0f, Color(75, 225, 207)),
        Paint::ColorStop(1.0f, Color(108, 126, 248))
    });
    canvas.drawText("Aa 123", textCard.getX() + 11.0f,
                    textCard.getY() + 32.0f, heroText);
    auto cjkText = makeTextPaint(compactCards ? 13.5f : 16.0f,
                                 Color(239, 244, 255), kCjkFamily);
    cjkText.setTextLocale("zh-CN");
    canvas.drawText(kComplexEmojiText,
                    textCard.getX() + 11.0f, textCard.getY() + 69.0f, cjkText);
    auto wrapText = makeTextPaint(compactCards ? 9.0f : 10.5f,
                                  Color(155, 178, 218));
    const float wrapTop = compactCards ? 91.0f : 98.0f;
    canvas.drawTextBox("Wrap, measure and ellipsis are rendered by the portable glyph atlas.",
                       RectF(textCard.getX() + 11.0f,
                             textCard.getY() + wrapTop,
                             cardWidth - 22.0f,
                             std::max(12.0f, cardHeight - wrapTop - 7.0f)),
                       compactCards ? 11.0f : 14.0f,
                       compactCards ? 1 : 3, true, wrapText);

    // 2. Concave path fill, cubic curve, stroke join and dash effect.
    const RectF pathCard = cardRect(1);
    drawCard(canvas, pathCard, "PATH + STROKE");
    const float pathCenterY = pathCard.getY()
        + (compactCards ? 77.0f : 91.0f);
    const float starRadius = compactCards ? 29.0f : 38.0f;
    Path star = makeStar(pathCard.getX() + 48.0f, pathCenterY,
                         starRadius, starRadius * 0.42f);
    Paint starFill;
    starFill.setStyle(Paint::Style::FILL);
    starFill.setLinearGradient(pathCard.getX() + 15.0f, pathCard.getY() + 55.0f,
                               pathCard.getX() + 82.0f, pathCard.getY() + 127.0f,
                               Color(255, 192, 80), Color(240, 82, 158));
    starFill.setAntiAlias(true);
    canvas.drawPath(star, starFill);
    Paint starStroke;
    starStroke.setStyle(Paint::Style::STROKE);
    starStroke.setStrokeColor(Color(255, 238, 192));
    starStroke.setStrokeWidth(2.0f);
    starStroke.setStrokeJoin(Paint::StrokeJoin::ROUND);
    canvas.drawPath(star, starStroke);
    Path curve;
    curve.moveTo(pathCard.getX() + 91.0f,
                 pathCard.getY() + (compactCards ? cardHeight - 16.0f : 116.0f));
    curve.cubicTo(pathCard.getX() + 111.0f,
                  pathCard.getY() + (compactCards ? 42.0f : 45.0f),
                  pathCard.getX() + 139.0f,
                  pathCard.getY() + (compactCards ? cardHeight - 9.0f : 145.0f),
                  pathCard.getX() + cardWidth - 13.0f,
                  pathCard.getY() + (compactCards ? 55.0f : 65.0f));
    Paint curveStroke;
    curveStroke.setStyle(Paint::Style::STROKE);
    curveStroke.setStrokeColor(Color(92, 213, 242));
    curveStroke.setStrokeWidth(4.0f);
    curveStroke.setStrokeCap(Paint::StrokeCap::ROUND);
    curveStroke.setDashPathEffect({9.0f, 6.0f}, elapsedSeconds * 12.0f);
    if (std::isfinite(elapsedSeconds)) {
        canvas.drawPath(curve, curveStroke);
    }

    // 3. Non-rectangular anti-aliased clip over a multi-stop gradient.
    const RectF clipCard = cardRect(2);
    drawCard(canvas, clipCard, "CLIP PATH + GRADIENT");
    const PointF clipCenter(clipCard.getX() + cardWidth * 0.5f,
                            clipCard.getY() + cardHeight * 0.62f);
    const float clipRadius = compactCards ? 36.0f : 58.0f;
    Path clipStar = makeStar(clipCenter.getX(), clipCenter.getY(),
                             clipRadius, clipRadius * 0.5f, 7);
    canvas.save();
    canvas.clipPath(clipStar);
    Paint clippedGradient;
    clippedGradient.setStyle(Paint::Style::FILL);
    clippedGradient.setLinearGradient(clipCenter.getX() - 60.0f, clipCenter.getY() - 55.0f,
                                      clipCenter.getX() + 60.0f, clipCenter.getY() + 55.0f, {
        Paint::ColorStop(0.0f, Color(60, 225, 194)),
        Paint::ColorStop(0.48f, Color(63, 148, 237)),
        Paint::ColorStop(1.0f, Color(174, 87, 235))
    });
    canvas.drawRect(RectF(clipCenter.getX() - clipRadius - 7.0f,
                          clipCenter.getY() - clipRadius - 2.0f,
                          (clipRadius + 7.0f) * 2.0f,
                          (clipRadius + 2.0f) * 2.0f), clippedGradient);
    Paint stripe;
    stripe.setStyle(Paint::Style::STROKE);
    stripe.setStrokeColor(Color(255, 255, 255, 80));
    stripe.setStrokeWidth(5.0f);
    for (int i = -3; i <= 4; ++i) {
        const float offset = static_cast<float>(i) * 22.0f;
        canvas.drawLine(clipCenter.getX() - clipRadius - 22.0f + offset,
                        clipCenter.getY() + clipRadius + 7.0f,
                        clipCenter.getX() + offset,
                        clipCenter.getY() - clipRadius - 7.0f, stripe);
    }
    canvas.restore();

    // 4. Arc modes, rounded caps and independent stroke widths.
    const RectF arcCard = cardRect(3);
    drawCard(canvas, arcCard, "ARCS + ROUND CAPS");
    const float arcSize = std::min(104.0f, cardHeight - 52.0f);
    const RectF arcBounds(arcCard.getX() + (cardWidth - arcSize) * 0.5f,
                          arcCard.getY() + 41.0f, arcSize, arcSize);
    Paint arcTrack;
    arcTrack.setStyle(Paint::Style::STROKE);
    arcTrack.setStrokeColor(Color(49, 64, 96));
    arcTrack.setStrokeWidth(12.0f);
    canvas.drawArc(arcBounds, -kPi * 0.85f, kPi * 1.7f,
                   wsc::Canvas::ArcMode::OPEN, arcTrack);
    Paint arcValue;
    arcValue.setStyle(Paint::Style::STROKE);
    arcValue.setStrokeColor(Color(67, 220, 198));
    arcValue.setStrokeWidth(12.0f);
    arcValue.setStrokeCap(Paint::StrokeCap::ROUND);
    if (std::isfinite(elapsedSeconds)) {
        canvas.drawArc(arcBounds, -kPi * 0.85f,
                       kPi * (0.35f + pulse * 1.25f),
                       wsc::Canvas::ArcMode::OPEN, arcValue);
    }
    Paint arcInner = arcValue;
    arcInner.setStrokeColor(Color(113, 130, 247));
    arcInner.setStrokeWidth(4.0f);
    canvas.drawArc(RectF(arcBounds.getX() + 17.0f, arcBounds.getY() + 17.0f,
                         arcBounds.getWidth() - 34.0f, arcBounds.getHeight() - 34.0f),
                   0.25f, kPi * 1.35f, wsc::Canvas::ArcMode::OPEN, arcInner);

    // 5. Save/restore with animated translate, rotate and scale transforms.
    const RectF transformCard = cardRect(4);
    drawCard(canvas, transformCard, "SAVE + TRANSFORM");
    const PointF transformCenter(transformCard.getX() + cardWidth * 0.5f,
                                 transformCard.getY() + cardHeight * 0.60f);
    if (std::isfinite(elapsedSeconds)) {
        canvas.save();
        canvas.translate(transformCenter.getX(), transformCenter.getY());
        canvas.rotate(elapsedSeconds * 0.45f);
        canvas.scale(0.92f + pulse * 0.14f, 0.92f + pulse * 0.14f);
        Paint rotated;
        rotated.setStyle(Paint::Style::FILL);
        rotated.setLinearGradient(-42.0f, -42.0f, 42.0f, 42.0f,
                                  Color(97, 121, 246), Color(69, 221, 203));
        const float transformHalfSize = compactCards ? 29.0f : 40.0f;
        canvas.drawRoundRect(RectF(-transformHalfSize, -transformHalfSize,
                                   transformHalfSize * 2.0f,
                                   transformHalfSize * 2.0f),
                             compactCards ? 11.0f : 15.0f, rotated);
        Paint axis;
        axis.setStyle(Paint::Style::STROKE);
        axis.setStrokeWidth(2.0f);
        axis.setStrokeColor(Color(255, 255, 255, 170));
        const float axisRadius = compactCards ? 38.0f : 54.0f;
        canvas.drawLine(-axisRadius, 0.0f, axisRadius, 0.0f, axis);
        canvas.drawLine(0.0f, -axisRadius, 0.0f, axisRadius, axis);
        canvas.restore();
    }

    // 6. Gaussian shadow plus SCREEN and MULTIPLY compositing.
    const RectF blendCard = cardRect(5);
    drawCard(canvas, blendCard, "SHADOW + BLEND");
    Paint shadowed;
    shadowed.setStyle(Paint::Style::FILL);
    shadowed.setFillColor(Color(255, 184, 76));
    shadowed.setShadowLayer(10.0f, 0.0f, 6.0f, Color(0, 0, 0, 180));
    const float blendSize = compactCards ? 44.0f : 64.0f;
    canvas.drawRoundRect(RectF(blendCard.getX() + 18.0f,
                              blendCard.getY() + (compactCards ? 47.0f : 57.0f),
                              blendSize, blendSize),
                         compactCards ? 13.0f : 18.0f, shadowed);
    Paint blendA;
    blendA.setStyle(Paint::Style::FILL);
    blendA.setFillColor(Color(71, 211, 229, 205));
    canvas.drawCircle(PointF(blendCard.getX() + 112.0f,
                             blendCard.getY() + (compactCards ? 61.0f : 78.0f)),
                      blendSize * 0.5f, blendA);
    Paint blendB;
    blendB.setStyle(Paint::Style::FILL);
    blendB.setFillColor(Color(178, 87, 238, 205));
    blendB.setBlendMode(Paint::BlendMode::SCREEN);
    canvas.drawCircle(PointF(blendCard.getX() + 136.0f,
                             blendCard.getY() + (compactCards ? 79.0f : 101.0f)),
                      blendSize * 0.5f, blendB);

    // 7. Raw RGBA upload, GPU texture sampling and rounded image clipping.
    const RectF imageCard = cardRect(6);
    drawCard(canvas, imageCard, "IMAGE + SAMPLING");
    if (checkerImage) {
        Paint imagePaint;
        imagePaint.setColor(Color::WHITE);
        imagePaint.setImageSampling(Paint::ImageSampling::NEAREST);
        imagePaint.setImageTileMode(Paint::ImageTileMode::REPEAT);
        canvas.drawImageTiled(*checkerImage,
                              RectF(imageCard.getX() + 13.0f, imageCard.getY() + 40.0f,
                                    cardWidth - 26.0f, cardHeight - 54.0f),
                              31.0f, 31.0f, imagePaint);
        canvas.drawImageRounded(*checkerImage,
                                RectF(imageCard.getX() + cardWidth - 71.0f,
                                      imageCard.getY() + cardHeight - 74.0f,
                                      56.0f, 56.0f),
                                18.0f, imagePaint);
    }

    // 8. Line AA, animated dash phase, point/line caps and progress geometry.
    const RectF motionCard = cardRect(7);
    drawCard(canvas, motionCard, "AA + DASH + MOTION");
    Path motionPath;
    motionPath.moveTo(motionCard.getX() + 14.0f, motionCard.getY() + 58.0f);
    motionPath.cubicTo(motionCard.getX() + 55.0f, motionCard.getY() + 28.0f,
                       motionCard.getX() + 96.0f, motionCard.getY() + 100.0f,
                       motionCard.getX() + cardWidth - 14.0f, motionCard.getY() + 61.0f);
    Paint dash;
    dash.setStyle(Paint::Style::STROKE);
    dash.setStrokeColor(Color(246, 204, 91));
    dash.setStrokeWidth(4.0f);
    dash.setStrokeCap(Paint::StrokeCap::ROUND);
    dash.setDashPathEffect({10.0f, 7.0f}, -elapsedSeconds * 18.0f);
    dash.setAntiAlias(true);
    if (std::isfinite(elapsedSeconds)) {
        canvas.drawPath(motionPath, dash);
    }
    const float trackX = motionCard.getX() + 14.0f;
    const float trackY = motionCard.getY() + cardHeight - 39.0f;
    const float trackWidth = cardWidth - 28.0f;
    Paint track;
    track.setStyle(Paint::Style::FILL);
    track.setFillColor(Color(7, 12, 28, 230));
    canvas.drawRoundRect(RectF(trackX, trackY, trackWidth, 12.0f), 6.0f, track);
    Paint progress;
    progress.setStyle(Paint::Style::FILL);
    progress.setLinearGradient(trackX, trackY, trackX + trackWidth, trackY,
                               Color(71, 222, 204), Color(105, 123, 247));
    if (std::isfinite(elapsedSeconds)) {
        canvas.drawRoundRect(RectF(trackX, trackY,
                                  trackWidth * (0.18f + pulse * 0.78f), 12.0f),
                             6.0f, progress);
    }

    auto footer = makeTextPaint(9.5f, Color(108, 132, 183));
    footer.setTextAlign(Paint::TextAlign::CENTER);
    canvas.drawText("8 feature cards  |  real OpenGL ES output",
                    width * 0.5f, height - 23.0f, footer);
}

// Only the genuinely changing overlays are rebuilt each frame. The static
// card chrome, text, paths and tracks live in a retained Picture.
void drawDynamicScene(wsc::Canvas& canvas, const wsc::Image* checkerImage,
                      float width, float height, float elapsedSeconds)
{
    using wsc::Color;
    using wsc::Paint;
    using wsc::Path;
    using wsc::PointF;
    using wsc::RectF;

    const float pulse = 0.5f + 0.5f * std::sin(elapsedSeconds * 2.2f);
    const float margin = 14.0f;
    const float gapX = 8.0f;
    const float gapY = 8.0f;
    const float gridTop = 69.0f;
    const float gridBottom = height - 35.0f;
    const bool landscape = width > height;
    const int columns = landscape ? 4 : 2;
    const int rows = landscape ? 2 : 4;
    const float cardWidth =
        (width - margin * 2.0f - gapX * static_cast<float>(columns - 1))
        / static_cast<float>(columns);
    const float cardHeight =
        (gridBottom - gridTop - gapY * static_cast<float>(rows - 1))
        / static_cast<float>(rows);
    const bool compactCards = cardHeight < 145.0f;
    auto cardRect = [&](int index) {
        const int column = index % columns;
        const int row = index / columns;
        return RectF(margin + static_cast<float>(column) * (cardWidth + gapX),
                     gridTop + static_cast<float>(row) * (cardHeight + gapY),
                     cardWidth, cardHeight);
    };

    const RectF pathCard = cardRect(1);
    Path curve;
    curve.moveTo(pathCard.getX() + 91.0f,
                 pathCard.getY() + (compactCards ? cardHeight - 16.0f : 116.0f));
    curve.cubicTo(pathCard.getX() + 111.0f,
                  pathCard.getY() + (compactCards ? 42.0f : 45.0f),
                  pathCard.getX() + 139.0f,
                  pathCard.getY() + (compactCards ? cardHeight - 9.0f : 145.0f),
                  pathCard.getX() + cardWidth - 13.0f,
                  pathCard.getY() + (compactCards ? 55.0f : 65.0f));
    Paint curveStroke;
    curveStroke.setStyle(Paint::Style::STROKE);
    curveStroke.setStrokeColor(Color(92, 213, 242));
    curveStroke.setStrokeWidth(4.0f);
    curveStroke.setStrokeCap(Paint::StrokeCap::ROUND);
    curveStroke.setDashPathEffect({9.0f, 6.0f}, elapsedSeconds * 12.0f);
    canvas.drawPath(curve, curveStroke);

    const RectF arcCard = cardRect(3);
    const float arcSize = std::min(104.0f, cardHeight - 52.0f);
    const RectF arcBounds(arcCard.getX() + (cardWidth - arcSize) * 0.5f,
                          arcCard.getY() + 41.0f, arcSize, arcSize);
    Paint arcValue;
    arcValue.setStyle(Paint::Style::STROKE);
    arcValue.setStrokeColor(Color(67, 220, 198));
    arcValue.setStrokeWidth(12.0f);
    arcValue.setStrokeCap(Paint::StrokeCap::ROUND);
    canvas.drawArc(arcBounds, -kPi * 0.85f,
                   kPi * (0.35f + pulse * 1.25f),
                   wsc::Canvas::ArcMode::OPEN, arcValue);

    const RectF transformCard = cardRect(4);
    const PointF transformCenter(transformCard.getX() + cardWidth * 0.5f,
                                 transformCard.getY() + cardHeight * 0.60f);
    canvas.save();
    canvas.translate(transformCenter.getX(), transformCenter.getY());
    canvas.rotate(elapsedSeconds * 0.45f);
    canvas.scale(0.92f + pulse * 0.14f, 0.92f + pulse * 0.14f);
    Paint rotated;
    rotated.setStyle(Paint::Style::FILL);
    rotated.setLinearGradient(-42.0f, -42.0f, 42.0f, 42.0f,
                              Color(97, 121, 246), Color(69, 221, 203));
    const float transformHalfSize = compactCards ? 29.0f : 40.0f;
    canvas.drawRoundRect(RectF(-transformHalfSize, -transformHalfSize,
                              transformHalfSize * 2.0f,
                              transformHalfSize * 2.0f),
                         compactCards ? 11.0f : 15.0f, rotated);
    Paint axis;
    axis.setStyle(Paint::Style::STROKE);
    axis.setStrokeWidth(2.0f);
    axis.setStrokeColor(Color(255, 255, 255, 170));
    const float axisRadius = compactCards ? 38.0f : 54.0f;
    canvas.drawLine(-axisRadius, 0.0f, axisRadius, 0.0f, axis);
    canvas.drawLine(0.0f, -axisRadius, 0.0f, axisRadius, axis);
    canvas.restore();

    const RectF imageCard = cardRect(6);
    if (checkerImage) {
        Paint imagePaint;
        imagePaint.setColor(Color::WHITE);
        imagePaint.setImageSampling(Paint::ImageSampling::NEAREST);
        imagePaint.setImageTileMode(Paint::ImageTileMode::REPEAT);
        canvas.drawImageTiled(*checkerImage,
                              RectF(imageCard.getX() + 13.0f, imageCard.getY() + 40.0f,
                                    cardWidth - 26.0f, cardHeight - 54.0f),
                              31.0f, 31.0f, imagePaint);
        canvas.drawImageRounded(*checkerImage,
                                RectF(imageCard.getX() + cardWidth - 71.0f,
                                      imageCard.getY() + cardHeight - 74.0f,
                                      56.0f, 56.0f),
                                18.0f, imagePaint);
    }

    const RectF motionCard = cardRect(7);
    Path motionPath;
    motionPath.moveTo(motionCard.getX() + 14.0f, motionCard.getY() + 58.0f);
    motionPath.cubicTo(motionCard.getX() + 55.0f, motionCard.getY() + 28.0f,
                       motionCard.getX() + 96.0f, motionCard.getY() + 100.0f,
                       motionCard.getX() + cardWidth - 14.0f, motionCard.getY() + 61.0f);
    Paint dash;
    dash.setStyle(Paint::Style::STROKE);
    dash.setStrokeColor(Color(246, 204, 91));
    dash.setStrokeWidth(4.0f);
    dash.setStrokeCap(Paint::StrokeCap::ROUND);
    dash.setDashPathEffect({10.0f, 7.0f}, -elapsedSeconds * 18.0f);
    dash.setAntiAlias(true);
    canvas.drawPath(motionPath, dash);
    const float trackX = motionCard.getX() + 14.0f;
    const float trackY = motionCard.getY() + cardHeight - 39.0f;
    const float trackWidth = cardWidth - 28.0f;
    Paint progress;
    progress.setStyle(Paint::Style::FILL);
    progress.setLinearGradient(trackX, trackY, trackX + trackWidth, trackY,
                               Color(71, 222, 204), Color(105, 123, 247));
    canvas.drawRoundRect(RectF(trackX, trackY,
                              trackWidth * (0.18f + pulse * 0.78f), 12.0f),
                         6.0f, progress);
}

class NativeRenderer {
public:
    bool surfaceCreated()
    {
        const EGLContext currentContext = eglGetCurrentContext();
        if (currentContext == EGL_NO_CONTEXT) {
            logError("surfaceCreated without a current EGL context");
            return false;
        }
        if (eglContext_ != EGL_NO_CONTEXT && eglContext_ != currentContext) {
            __android_log_print(ANDROID_LOG_WARN, kLogTag,
                                "EGL context changed; rebuilding derived resources");
            abandon();
        }
        eglContext_ = currentContext;
        const bool loaded = wsc::Canvas::loadOpenGL(&loadOpenGlesProcedure);
        if (!loaded) {
            logError("Canvas::loadOpenGL failed for the active OpenGL ES context");
        }
        return loaded;
    }

    bool resize(int width, int height, float density)
    {
        if (width <= 0 || height <= 0) {
            return false;
        }

        const float safeDensity = std::isfinite(density) && density > 0.0f
            ? density : 1.0f;
        glViewport(0, 0, width, height);
        if (canvas_ && width == physicalWidth_ && height == physicalHeight_
            && std::abs(safeDensity - density_) < 0.001f) {
            return true;
        }

        const EGLContext currentContext = eglGetCurrentContext();
        release();
        eglContext_ = currentContext;
        physicalWidth_ = width;
        physicalHeight_ = height;
        density_ = safeDensity;
        logicalWidth_ = static_cast<float>(width) / safeDensity;
        logicalHeight_ = static_cast<float>(height) / safeDensity;
        const auto viewport = whatscanvas::scenes::makeCanonicalViewport(
            logicalWidth_, logicalHeight_);
        sceneWidth_ = viewport.width;
        sceneHeight_ = viewport.height;
        sceneScale_ = viewport.scale;
        sceneOffsetX_ = viewport.offsetX;
        sceneOffsetY_ = viewport.offsetY;

        canvas_ = wsc::Canvas::create(
            wsc::Canvas::Backend::OpenGLES, width, height);
        if (!canvas_) {
            logError("Canvas::create(OpenGLES) returned null");
            return false;
        }

        canvas_->setDevicePixelRatio(safeDensity);
        if (!canvas_->initializeContext()) {
            logError("Canvas::initializeContext failed");
            canvas_.reset();
            return false;
        }
        createCheckerImage(*canvas_, checkerImage_);
        staticPicture_ = canvas_->recordPicture(
            [&](wsc::Canvas& recordingCanvas) {
                recordingCanvas.drawColor(wsc::Color(8, 12, 29));
                recordingCanvas.save();
                recordingCanvas.translate(sceneOffsetX_, sceneOffsetY_);
                recordingCanvas.scale(sceneScale_, sceneScale_);
                drawScene(recordingCanvas, nullptr, sceneWidth_, sceneHeight_,
                          std::numeric_limits<float>::quiet_NaN());
                recordingCanvas.restore();
            });
        if (!staticPicture_) {
            logError("Static Picture recording failed");
            canvas_->finalizeContext();
            canvas_.reset();
            return false;
        }

        auto probePaint = makeTextPaint(18.0f, wsc::Color::WHITE);
        const float probeWidth = canvas_->measureText(
            "WhatsCanvas Aa 123", probePaint);
        auto cjkProbePaint = makeTextPaint(
            18.0f, wsc::Color::WHITE, kCjkFamily);
        cjkProbePaint.setTextLocale("zh-CN");
        const float cjkProbeWidth = canvas_->measureText(
            "\xE4\xB8\xAD\xE6\x96\x87", cjkProbePaint);
        auto boldProbePaint = probePaint;
        boldProbePaint.setFontWeight(700);
        const float boldProbeWidth = canvas_->measureText(
            "Bold 700", boldProbePaint);
        const float emojiProbeWidth = canvas_->measureText(
            "\xF0\x9F\x98\x80\xEF\xB8\x8F", cjkProbePaint);
        const float complexEmojiProbeWidth = canvas_->measureText(
            kComplexEmojiText, cjkProbePaint);
        __android_log_print(
            ANDROID_LOG_INFO, kLogTag,
            "OpenGLES renderer ready: %dx%d, dpr=%.2f, GL=%s, "
            "latinWidth=%.1f, zhCnWidth=%.1f, boldWidth=%.1f, emojiWidth=%.1f, "
            "complexEmojiWidth=%.1f, pictureOps=%zu",
            width, height, density,
            wsc::Canvas::getOpenGLVersionString().c_str(), probeWidth,
            cjkProbeWidth, boldProbeWidth, emojiProbeWidth,
            complexEmojiProbeWidth, staticPicture_->operationCount());
        return true;
    }

    void render(float elapsedSeconds)
    {
        if (!canvas_) {
            return;
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.03f, 0.04f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        canvas_->beginFrame();
        const auto recordStart = std::chrono::steady_clock::now();
        // The retained scene is an isolated, opaque full-screen layer. Cache
        // its raster result like Flutter's RepaintBoundary/RasterCache so the
        // steady state submits one textured quad instead of ~100 static draws.
        canvas_->drawPictureRasterized(*staticPicture_);
        const auto pictureEnd = std::chrono::steady_clock::now();
        canvas_->save();
        canvas_->translate(sceneOffsetX_, sceneOffsetY_);
        canvas_->scale(sceneScale_, sceneScale_);
        drawDynamicScene(*canvas_, checkerImage_.get(), sceneWidth_, sceneHeight_,
                         elapsedSeconds);
        canvas_->restore();
        const auto recordEnd = std::chrono::steady_clock::now();
        canvas_->endFrame();
        lastRecordCpuTimeUs_ = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                recordEnd - recordStart).count());
        lastPictureCpuTimeUs_ = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                pictureEnd - recordStart).count());
        lastDynamicCpuTimeUs_ = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                recordEnd - pictureEnd).count());

        const int elapsedSecond = static_cast<int>(elapsedSeconds);
        if (elapsedSecond != lastStatsSecond_ && elapsedSecond % 5 == 0) {
            lastStatsSecond_ = elapsedSecond;
            const auto stats = canvas_->getRenderStats();
            __android_log_print(
                ANDROID_LOG_INFO, kLogTag,
                "Frame stats: commands=%zu draws=%zu paths=%zu uploads=%zu/%zuKB "
                "sprites=%zu/%zu/%zuKB "
                "fillAA=%zu/%zu/%zuKB strokeAA=%zu/%zu/%zuKB "
                "glyphTextures=%zu images=%zu filters=%zu pictureCache=%zu/%zu "
                "textCache=%zu/%zu/%zu/%zu glyphs=%zu/%zu/%zu dirty=%zuKB "
                "textStages=%llu/%llu/%llu/%llu/%llu/%lluus "
                "shapeStages=%llu/%llu/%llu/%lluus "
                "rasterCache=%zu/%zu/%zu/%zuKB/%zuevict "
                "rasterCpu=%llu/%llu/%llu path=%llu text=%llu(%llu+%llu)us "
                "shaders=%zu/%zu/%llu+%lluus "
                "recordCpu=%lluus pictureCpu=%lluus dynamicCpu=%lluus flushCpu=%lluus",
                stats.commandCount, stats.drawCallCount,
                stats.pathVertexCount, stats.pathUploadCount,
                stats.pathUploadBytes / 1024u,
                stats.imageBatchQuadCount,
                stats.imageBatchInstancedQuadCount,
                stats.imageBatchUploadBytes / 1024u,
                stats.aaCacheHits, stats.aaCacheMisses,
                stats.aaCacheBytes / 1024u,
                stats.strokeAaCacheHits, stats.strokeAaCacheMisses,
                stats.strokeAaCacheBytes / 1024u,
                stats.glyphAtlasTextureCount,
                stats.imageTextureCount,
                stats.filterCount,
                stats.retainedPictureCacheHits,
                stats.retainedPictureCacheMisses,
                stats.textShapeCacheHits,
                stats.textShapeCacheMisses,
                stats.textLayoutCacheHits,
                stats.textLayoutCacheMisses,
                stats.glyphAtlasHits,
                stats.glyphAtlasMisses,
                stats.glyphRasterizationCount,
                stats.glyphAtlasDirtyBytes / 1024u,
                static_cast<unsigned long long>(stats.textNormalizationCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.textLayoutCacheCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.textShapingCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.glyphCacheLookupCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.glyphRasterCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.glyphAtlasUploadCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.textBidiCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.textFontFallbackCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.textFontDataCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.textShapeEngineCpuTimeNs / 1000u),
                stats.retainedPictureRasterCacheHits,
                stats.retainedPictureRasterCacheMisses,
                stats.retainedPictureRasterCacheSize,
                stats.retainedPictureRasterCacheBytes / 1024u,
                stats.retainedPictureRasterCacheEvictions,
                static_cast<unsigned long long>(stats.retainedPictureRasterPrepareCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.retainedPictureRasterBoundsCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.retainedPictureRasterRenderCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.retainedPictureRasterPathCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.retainedPictureRasterTextCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.retainedPictureRasterTextBackendCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.retainedPictureRasterTextAtlasCpuTimeNs / 1000u),
                stats.shaderProgramLinkCount,
                stats.shaderStageCompileCount,
                static_cast<unsigned long long>(stats.shaderCompileCpuTimeNs / 1000u),
                static_cast<unsigned long long>(stats.shaderLinkCpuTimeNs / 1000u),
                static_cast<unsigned long long>(lastRecordCpuTimeUs_),
                static_cast<unsigned long long>(lastPictureCpuTimeUs_),
                static_cast<unsigned long long>(lastDynamicCpuTimeUs_),
                static_cast<unsigned long long>(stats.flushCpuTimeNs / 1000u));
        }
    }

    void release()
    {
        checkerImage_.reset();
        if (canvas_) {
            // Purge Picture-derived textures while their owning context is
            // still current, then destroy other context-bound resources.
            canvas_->finalizeContext();
        }
        staticPicture_.reset();
        canvas_.reset();
        eglContext_ = EGL_NO_CONTEXT;
        physicalWidth_ = 0;
        physicalHeight_ = 0;
        density_ = 1.0f;
        lastStatsSecond_ = -1;
    }

    void abandon()
    {
        // The previous EGLContext is already gone. Mark every resource from
        // that generation invalid before releasing CPU owners, so no GL delete
        // is accidentally issued against the replacement context.
        if (canvas_) {
            canvas_->abandonContext();
        }
        checkerImage_.reset();
        staticPicture_.reset();
        canvas_.reset();
        eglContext_ = EGL_NO_CONTEXT;
        physicalWidth_ = 0;
        physicalHeight_ = 0;
        density_ = 1.0f;
        lastStatsSecond_ = -1;
    }

private:
    std::unique_ptr<wsc::Canvas> canvas_;
    std::unique_ptr<wsc::Image> checkerImage_;
    std::shared_ptr<const wsc::Picture> staticPicture_;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    int physicalWidth_ = 0;
    int physicalHeight_ = 0;
    float density_ = 1.0f;
    float logicalWidth_ = 1.0f;
    float logicalHeight_ = 1.0f;
    float sceneWidth_ = 1.0f;
    float sceneHeight_ = 1.0f;
    float sceneScale_ = 1.0f;
    float sceneOffsetX_ = 0.0f;
    float sceneOffsetY_ = 0.0f;
    int lastStatsSecond_ = -1;
    std::uint64_t lastRecordCpuTimeUs_ = 0;
    std::uint64_t lastPictureCpuTimeUs_ = 0;
    std::uint64_t lastDynamicCpuTimeUs_ = 0;
};

NativeRenderer* rendererFromHandle(jlong handle)
{
    return reinterpret_cast<NativeRenderer*>(
        static_cast<std::uintptr_t>(handle));
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeCreate(
    JNIEnv*, jobject)
{
    wsc::Log::setLevel(wsc::LogLevel::Info);
    wsc::Log::setHandler([](const wsc::LogMessage &message) {
        __android_log_print(
            androidLogPriority(message.level), kLogTag,
            "%s: %s", message.category, message.message.c_str());
    });
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(
        new NativeRenderer()));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeSurfaceCreated(
    JNIEnv*, jobject, jlong handle)
{
    NativeRenderer* renderer = rendererFromHandle(handle);
    return renderer && renderer->surfaceCreated() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeResize(
    JNIEnv*, jobject, jlong handle, jint width, jint height, jfloat density)
{
    NativeRenderer* renderer = rendererFromHandle(handle);
    return renderer && renderer->resize(width, height, density)
        ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeRender(
    JNIEnv*, jobject, jlong handle, jfloat elapsedSeconds)
{
    NativeRenderer* renderer = rendererFromHandle(handle);
    if (renderer) {
        renderer->render(elapsedSeconds);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_whatscanvas_demo_WhatsCanvasRenderer_nativeDestroy(
    JNIEnv*, jobject, jlong handle)
{
    NativeRenderer* renderer = rendererFromHandle(handle);
    if (renderer) {
        renderer->release();
        delete renderer;
    }
}
