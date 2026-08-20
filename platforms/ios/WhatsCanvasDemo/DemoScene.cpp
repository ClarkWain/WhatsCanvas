#include "DemoScene.h"

#include <wsc/wsc.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace whatscanvas::demo {
namespace {

constexpr float kPi = 3.14159265358979323846f;

constexpr const char *kSansFamily = "Helvetica Neue";
constexpr const char *kCjkFamily = "PingFang SC";
constexpr const char *kComplexEmojiText =
    "\xE4\xB8\xAD\xE6\x96\x87 "
    "\xF0\x9F\x91\xA9\xF0\x9F\x8F\xBD\xE2\x80\x8D\xF0\x9F\x92\xBB "
    "\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3 8\xEF\xB8\x8F\xE2\x83\xA3";

wsc::Paint textPaint(float size, const wsc::Color &color,
                     const char *family = kSansFamily)
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
                   float innerRadius, int pointCount = 5)
{
    wsc::Path path;
    for (int index = 0; index < pointCount * 2; ++index) {
        const float radius = index % 2 == 0 ? outerRadius : innerRadius;
        const float angle = -kPi * 0.5f
            + static_cast<float>(index) * kPi / static_cast<float>(pointCount);
        const float x = centerX + std::cos(angle) * radius;
        const float y = centerY + std::sin(angle) * radius;
        if (index == 0) path.moveTo(x, y); else path.lineTo(x, y);
    }
    path.close();
    return path;
}

struct Layout
{
    float width;
    float height;
    float safeTop;
    float safeBottom;
    float safeLeft;
    float safeRight;
    int columns;
    int rows;
    float margin;
    float gapX;
    float gapY;
    float gridTop;
    float gridBottom;
    float cardWidth;
    float cardHeight;
    bool compactCards;

    Layout(float widthIn, float heightIn, float safeTopIn, float safeBottomIn,
           float safeLeftIn, float safeRightIn)
        : width(widthIn), height(heightIn),
          safeTop(safeTopIn), safeBottom(safeBottomIn),
          safeLeft(safeLeftIn), safeRight(safeRightIn),
          margin(14.0f), gapX(8.0f), gapY(8.0f)
    {
        const bool landscape = width > height;
        columns = landscape ? 4 : 2;
        rows = landscape ? 2 : 4;
        gridTop = safeTop + 69.0f;
        gridBottom = height - safeBottom - 35.0f;
        cardWidth = (width - safeLeft - safeRight - margin * 2.0f
                     - gapX * static_cast<float>(columns - 1))
                    / static_cast<float>(columns);
        cardHeight = (gridBottom - gridTop
                      - gapY * static_cast<float>(rows - 1))
                     / static_cast<float>(rows);
        compactCards = cardHeight < 145.0f;
    }

    wsc::RectF card(int index) const
    {
        const int column = index % columns;
        const int row = index / columns;
        return wsc::RectF(
            safeLeft + margin + static_cast<float>(column) * (cardWidth + gapX),
            gridTop + static_cast<float>(row) * (cardHeight + gapY),
            cardWidth, cardHeight);
    }
};

void drawCard(wsc::Canvas &canvas, const wsc::RectF &rect,
              const std::string &label)
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

    auto labelPaint = textPaint(10.0f, wsc::Color(139, 166, 226));
    labelPaint.setLetterSpacing(0.7f);
    canvas.drawText(label, rect.getX() + 11.0f, rect.getY() + 9.0f, labelPaint);
}

void drawScene(wsc::Canvas &canvas, const wsc::Image *checkerImage,
               const Layout &grid, float elapsedSeconds)
{
    using wsc::Color;
    using wsc::Paint;
    using wsc::Path;
    using wsc::PointF;
    using wsc::RectF;

    const float width = grid.width;
    const float height = grid.height;
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
    canvas.drawCircle(PointF(width * 0.12f, height * 0.2f),
                      shortSide * 0.38f, ambientGlow);
    ambientGlow.setFillColor(Color(150, 86, 232, 20));
    canvas.drawCircle(PointF(width * 0.92f, height * 0.78f),
                      shortSide * 0.42f, ambientGlow);

    auto title = textPaint(23.0f, Color(242, 247, 255));
    title.setFontWeight(400);
    canvas.drawText("WhatsCanvas iOS", grid.safeLeft + 15.0f,
                    grid.safeTop + 13.0f, title);
    auto subtitle = textPaint(10.5f, Color(130, 157, 211));
    canvas.drawText("Metal  |  CoreText  |  60 FPS",
                    grid.safeLeft + 16.0f, grid.safeTop + 44.0f, subtitle);

    Paint livePill;
    livePill.setStyle(Paint::Style::FILL);
    livePill.setFillColor(Color(43, 204, 164, 42));
    const RectF liveRect(width - grid.safeRight - 66.0f,
                         grid.safeTop + 17.0f, 51.0f, 21.0f);
    canvas.drawRoundRect(liveRect, 10.5f, livePill);
    auto liveText = textPaint(9.0f, Color(83, 235, 196));
    liveText.setTextAlign(Paint::TextAlign::CENTER);
    canvas.drawText("60 FPS",
                    liveRect.getX() + liveRect.getWidth() * 0.5f,
                    liveRect.getY() + 5.0f, liveText);

    // 1. Portable glyph atlas, font fallback, gradients and text layout.
    const RectF textCard = grid.card(0);
    drawCard(canvas, textCard, "CORETEXT + UTF-8");
    auto heroText = textPaint(grid.compactCards ? 23.0f : 27.0f, Color::WHITE);
    heroText.setLinearGradient(textCard.getX() + 11.0f, 0.0f,
                               textCard.getX() + grid.cardWidth - 12.0f, 0.0f, {
        Paint::ColorStop(0.0f, Color(75, 225, 207)),
        Paint::ColorStop(1.0f, Color(108, 126, 248))
    });
    canvas.drawText("Aa 123", textCard.getX() + 11.0f,
                    textCard.getY() + 32.0f, heroText);
    auto cjkText = textPaint(grid.compactCards ? 13.5f : 16.0f,
                             Color(239, 244, 255), kCjkFamily);
    cjkText.setTextLocale("zh-CN");
    canvas.drawText(kComplexEmojiText, textCard.getX() + 11.0f,
                    textCard.getY() + 69.0f, cjkText);
    auto wrapText = textPaint(grid.compactCards ? 9.0f : 10.5f,
                              Color(155, 178, 218));
    const float wrapTop = grid.compactCards ? 91.0f : 98.0f;
    canvas.drawTextBox(
        "Native layout, fallback and cached bitmap raster.",
        RectF(textCard.getX() + 11.0f, textCard.getY() + wrapTop,
              grid.cardWidth - 22.0f,
              std::max(12.0f, grid.cardHeight - wrapTop - 7.0f)),
        grid.compactCards ? 11.0f : 14.0f,
        grid.compactCards ? 1 : 3, true, wrapText);

    // 2. Concave path fill, cubic curve, stroke join and dash effect.
    const RectF pathCard = grid.card(1);
    drawCard(canvas, pathCard, "PATH + STROKE");
    const float pathCenterY = pathCard.getY()
        + (grid.compactCards ? 77.0f : 91.0f);
    const float starRadius = grid.compactCards ? 29.0f : 38.0f;
    Path star = makeStar(pathCard.getX() + 48.0f, pathCenterY,
                         starRadius, starRadius * 0.42f);
    Paint starFill;
    starFill.setStyle(Paint::Style::FILL);
    starFill.setLinearGradient(pathCard.getX() + 15.0f,
                               pathCard.getY() + 55.0f,
                               pathCard.getX() + 82.0f,
                               pathCard.getY() + 127.0f,
                               Color(255, 192, 80), Color(240, 82, 158));
    starFill.setAntiAlias(true);
    canvas.drawPath(star, starFill);
    Paint starStroke;
    starStroke.setStyle(Paint::Style::STROKE);
    starStroke.setStrokeColor(Color(255, 238, 192));
    starStroke.setStrokeWidth(2.0f);
    starStroke.setStrokeJoin(Paint::StrokeJoin::ROUND);
    canvas.drawPath(star, starStroke);
    if (std::isfinite(elapsedSeconds)) {
        Path curve;
        curve.moveTo(pathCard.getX() + 91.0f,
                     pathCard.getY()
                         + (grid.compactCards ? grid.cardHeight - 16.0f : 116.0f));
        curve.cubicTo(pathCard.getX() + 111.0f,
                      pathCard.getY() + (grid.compactCards ? 42.0f : 45.0f),
                      pathCard.getX() + 139.0f,
                      pathCard.getY()
                          + (grid.compactCards ? grid.cardHeight - 9.0f : 145.0f),
                      pathCard.getX() + grid.cardWidth - 13.0f,
                      pathCard.getY() + (grid.compactCards ? 55.0f : 65.0f));
        Paint curveStroke;
        curveStroke.setStyle(Paint::Style::STROKE);
        curveStroke.setStrokeColor(Color(92, 213, 242));
        curveStroke.setStrokeWidth(4.0f);
        curveStroke.setStrokeCap(Paint::StrokeCap::ROUND);
        curveStroke.setDashPathEffect({9.0f, 6.0f}, elapsedSeconds * 12.0f);
        canvas.drawPath(curve, curveStroke);
    }

    // 3. Non-rectangular anti-aliased clip over a multi-stop gradient.
    const RectF clipCard = grid.card(2);
    drawCard(canvas, clipCard, "CLIP PATH + GRADIENT");
    const PointF clipCenter(clipCard.getX() + grid.cardWidth * 0.5f,
                            clipCard.getY() + grid.cardHeight * 0.62f);
    const float clipRadius = grid.compactCards ? 36.0f : 58.0f;
    Path clipStar = makeStar(clipCenter.getX(), clipCenter.getY(),
                             clipRadius, clipRadius * 0.5f, 7);
    canvas.save();
    canvas.clipPath(clipStar);
    Paint clippedGradient;
    clippedGradient.setStyle(Paint::Style::FILL);
    clippedGradient.setLinearGradient(
        clipCenter.getX() - 60.0f, clipCenter.getY() - 55.0f,
        clipCenter.getX() + 60.0f, clipCenter.getY() + 55.0f, {
            Paint::ColorStop(0.0f, Color(60, 225, 194)),
            Paint::ColorStop(0.48f, Color(63, 148, 237)),
            Paint::ColorStop(1.0f, Color(174, 87, 235))
        });
    canvas.drawRect(RectF(clipCenter.getX() - clipRadius - 7.0f,
                          clipCenter.getY() - clipRadius - 2.0f,
                          (clipRadius + 7.0f) * 2.0f,
                          (clipRadius + 2.0f) * 2.0f),
                    clippedGradient);
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
    const RectF arcCard = grid.card(3);
    drawCard(canvas, arcCard, "ARCS + ROUND CAPS");
    const float arcSize = std::min(104.0f, grid.cardHeight - 52.0f);
    const RectF arcBounds(arcCard.getX() + (grid.cardWidth - arcSize) * 0.5f,
                          arcCard.getY() + 41.0f, arcSize, arcSize);
    Paint arcTrack;
    arcTrack.setStyle(Paint::Style::STROKE);
    arcTrack.setStrokeColor(Color(49, 64, 96));
    arcTrack.setStrokeWidth(12.0f);
    canvas.drawArc(arcBounds, -kPi * 0.85f, kPi * 1.7f,
                   wsc::Canvas::ArcMode::OPEN, arcTrack);
    if (std::isfinite(elapsedSeconds)) {
        Paint arcValue;
        arcValue.setStyle(Paint::Style::STROKE);
        arcValue.setStrokeColor(Color(67, 220, 198));
        arcValue.setStrokeWidth(12.0f);
        arcValue.setStrokeCap(Paint::StrokeCap::ROUND);
        canvas.drawArc(arcBounds, -kPi * 0.85f,
                       kPi * (0.35f + pulse * 1.25f),
                       wsc::Canvas::ArcMode::OPEN, arcValue);
    }
    Paint arcInner;
    arcInner.setStyle(Paint::Style::STROKE);
    arcInner.setStrokeColor(Color(113, 130, 247));
    arcInner.setStrokeWidth(4.0f);
    arcInner.setStrokeCap(Paint::StrokeCap::ROUND);
    canvas.drawArc(RectF(arcBounds.getX() + 17.0f, arcBounds.getY() + 17.0f,
                         arcBounds.getWidth() - 34.0f,
                         arcBounds.getHeight() - 34.0f),
                   0.25f, kPi * 1.35f, wsc::Canvas::ArcMode::OPEN, arcInner);

    // 5. Save/restore with animated translate, rotate and scale transforms.
    const RectF transformCard = grid.card(4);
    drawCard(canvas, transformCard, "SAVE + TRANSFORM");
    const PointF transformCenter(transformCard.getX() + grid.cardWidth * 0.5f,
                                 transformCard.getY() + grid.cardHeight * 0.60f);
    if (std::isfinite(elapsedSeconds)) {
        canvas.save();
        canvas.translate(transformCenter.getX(), transformCenter.getY());
        canvas.rotate(elapsedSeconds * 0.45f);
        canvas.scale(0.92f + pulse * 0.14f, 0.92f + pulse * 0.14f);
        Paint rotated;
        rotated.setStyle(Paint::Style::FILL);
        rotated.setLinearGradient(-42.0f, -42.0f, 42.0f, 42.0f,
                                  Color(97, 121, 246), Color(69, 221, 203));
        const float transformHalfSize = grid.compactCards ? 29.0f : 40.0f;
        canvas.drawRoundRect(RectF(-transformHalfSize, -transformHalfSize,
                                   transformHalfSize * 2.0f,
                                   transformHalfSize * 2.0f),
                             grid.compactCards ? 11.0f : 15.0f, rotated);
        Paint axis;
        axis.setStyle(Paint::Style::STROKE);
        axis.setStrokeWidth(2.0f);
        axis.setStrokeColor(Color(255, 255, 255, 170));
        const float axisRadius = grid.compactCards ? 38.0f : 54.0f;
        canvas.drawLine(-axisRadius, 0.0f, axisRadius, 0.0f, axis);
        canvas.drawLine(0.0f, -axisRadius, 0.0f, axisRadius, axis);
        canvas.restore();
    }

    // 6. Gaussian shadow plus SCREEN compositing.
    const RectF blendCard = grid.card(5);
    drawCard(canvas, blendCard, "SHADOW + BLEND");
    Paint shadowed;
    shadowed.setStyle(Paint::Style::FILL);
    shadowed.setFillColor(Color(255, 184, 76));
    shadowed.setShadowLayer(10.0f, 0.0f, 6.0f, Color(0, 0, 0, 180));
    const float blendSize = grid.compactCards ? 44.0f : 64.0f;
    canvas.drawRoundRect(RectF(blendCard.getX() + 18.0f,
                               blendCard.getY()
                                   + (grid.compactCards ? 47.0f : 57.0f),
                               blendSize, blendSize),
                         grid.compactCards ? 13.0f : 18.0f, shadowed);
    Paint blendA;
    blendA.setStyle(Paint::Style::FILL);
    blendA.setFillColor(Color(71, 211, 229, 205));
    canvas.drawCircle(PointF(blendCard.getX() + 112.0f,
                             blendCard.getY()
                                 + (grid.compactCards ? 61.0f : 78.0f)),
                      blendSize * 0.5f, blendA);
    Paint blendB;
    blendB.setStyle(Paint::Style::FILL);
    blendB.setFillColor(Color(178, 87, 238, 205));
    blendB.setBlendMode(Paint::BlendMode::SCREEN);
    canvas.drawCircle(PointF(blendCard.getX() + 136.0f,
                             blendCard.getY()
                                 + (grid.compactCards ? 79.0f : 101.0f)),
                      blendSize * 0.5f, blendB);

    // 7. Raw RGBA upload, GPU texture sampling and rounded image clipping.
    const RectF imageCard = grid.card(6);
    drawCard(canvas, imageCard, "IMAGE + SAMPLING");
    if (checkerImage != nullptr) {
        Paint imagePaint;
        imagePaint.setColor(Color::WHITE);
        imagePaint.setImageSampling(Paint::ImageSampling::NEAREST);
        canvas.drawImageTiled(*checkerImage,
                              RectF(imageCard.getX() + 13.0f,
                                    imageCard.getY() + 40.0f,
                                    grid.cardWidth - 26.0f,
                                    grid.cardHeight - 54.0f),
                              31.0f, 31.0f, imagePaint);
        canvas.drawImageRounded(*checkerImage,
                                RectF(imageCard.getX() + grid.cardWidth - 71.0f,
                                      imageCard.getY() + grid.cardHeight - 74.0f,
                                      56.0f, 56.0f),
                                18.0f, imagePaint);
    }

    // 8. Line AA, animated dash phase, point/line caps and progress geometry.
    const RectF motionCard = grid.card(7);
    drawCard(canvas, motionCard, "AA + DASH + MOTION");
    if (std::isfinite(elapsedSeconds)) {
        Path motionPath;
        motionPath.moveTo(motionCard.getX() + 14.0f,
                          motionCard.getY() + 58.0f);
        motionPath.cubicTo(motionCard.getX() + 55.0f,
                           motionCard.getY() + 28.0f,
                           motionCard.getX() + 96.0f,
                           motionCard.getY() + 100.0f,
                           motionCard.getX() + grid.cardWidth - 14.0f,
                           motionCard.getY() + 61.0f);
        Paint dash;
        dash.setStyle(Paint::Style::STROKE);
        dash.setStrokeColor(Color(246, 204, 91));
        dash.setStrokeWidth(4.0f);
        dash.setStrokeCap(Paint::StrokeCap::ROUND);
        dash.setDashPathEffect({10.0f, 7.0f}, -elapsedSeconds * 18.0f);
        dash.setAntiAlias(true);
        canvas.drawPath(motionPath, dash);
    }
    const float trackX = motionCard.getX() + 14.0f;
    const float trackY = motionCard.getY() + grid.cardHeight - 39.0f;
    const float trackWidth = grid.cardWidth - 28.0f;
    Paint track;
    track.setStyle(Paint::Style::FILL);
    track.setFillColor(Color(7, 12, 28, 230));
    canvas.drawRoundRect(RectF(trackX, trackY, trackWidth, 12.0f),
                         6.0f, track);
    if (std::isfinite(elapsedSeconds)) {
        Paint progress;
        progress.setStyle(Paint::Style::FILL);
        progress.setLinearGradient(trackX, trackY, trackX + trackWidth, trackY,
                                   Color(71, 222, 204), Color(105, 123, 247));
        canvas.drawRoundRect(RectF(trackX, trackY,
                                   trackWidth * (0.18f + pulse * 0.78f), 12.0f),
                             6.0f, progress);
    }

    auto footer = textPaint(9.5f, Color(108, 132, 183));
    footer.setTextAlign(Paint::TextAlign::CENTER);
    canvas.drawText("8 feature cards  |  native Metal output",
                    (grid.safeLeft + width - grid.safeRight) * 0.5f,
                    height - grid.safeBottom - 23.0f, footer);
}

} // namespace

void createCheckerImage(wsc::Canvas &canvas, std::unique_ptr<wsc::Image> &image)
{
    constexpr int size = 24;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size * size * 4));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool alternate = ((x / 6) + (y / 6)) % 2 != 0;
            const std::size_t offset =
                static_cast<std::size_t>((y * size + x) * 4);
            pixels[offset] = alternate ? 78 : 32;
            pixels[offset + 1] = alternate ? 218 : 98;
            pixels[offset + 2] = alternate ? 206 : 230;
            pixels[offset + 3] = 255;
        }
    }
    image = std::make_unique<wsc::Image>();
    if (!canvas.loadImageFromRGBA(*image, pixels, size, size, true)) image.reset();
}

std::shared_ptr<const wsc::Picture> recordStaticScene(wsc::Canvas &canvas,
                                                      float width, float height,
                                                      float safeTop,
                                                      float safeBottom,
                                                      float safeLeft,
                                                      float safeRight)
{
    const Layout grid(width, height, safeTop, safeBottom, safeLeft, safeRight);
    return canvas.recordPicture([grid](wsc::Canvas &recording) {
        drawScene(recording, nullptr, grid,
                  std::numeric_limits<float>::quiet_NaN());
    });
}

void drawDynamicScene(wsc::Canvas &canvas, const wsc::Image *checkerImage,
                      float width, float height, float safeTop,
                      float safeBottom, float safeLeft, float safeRight,
                      float elapsedSeconds)
{
    using wsc::Color;
    using wsc::Paint;
    using wsc::Path;
    using wsc::PointF;
    using wsc::RectF;

    const Layout grid(width, height, safeTop, safeBottom, safeLeft, safeRight);
    const float pulse = 0.5f + 0.5f * std::sin(elapsedSeconds * 2.2f);

    // Card 1 (PATH + STROKE): animated dashed cubic curve.
    const RectF pathCard = grid.card(1);
    Path curve;
    curve.moveTo(pathCard.getX() + 91.0f,
                 pathCard.getY()
                     + (grid.compactCards ? grid.cardHeight - 16.0f : 116.0f));
    curve.cubicTo(pathCard.getX() + 111.0f,
                  pathCard.getY() + (grid.compactCards ? 42.0f : 45.0f),
                  pathCard.getX() + 139.0f,
                  pathCard.getY()
                      + (grid.compactCards ? grid.cardHeight - 9.0f : 145.0f),
                  pathCard.getX() + grid.cardWidth - 13.0f,
                  pathCard.getY() + (grid.compactCards ? 55.0f : 65.0f));
    Paint curveStroke;
    curveStroke.setStyle(Paint::Style::STROKE);
    curveStroke.setStrokeColor(Color(92, 213, 242));
    curveStroke.setStrokeWidth(4.0f);
    curveStroke.setStrokeCap(Paint::StrokeCap::ROUND);
    curveStroke.setDashPathEffect({9.0f, 6.0f}, elapsedSeconds * 12.0f);
    canvas.drawPath(curve, curveStroke);

    // Card 3 (ARCS + ROUND CAPS): animated value arc.
    const RectF arcCard = grid.card(3);
    const float arcSize = std::min(104.0f, grid.cardHeight - 52.0f);
    const RectF arcBounds(arcCard.getX() + (grid.cardWidth - arcSize) * 0.5f,
                          arcCard.getY() + 41.0f, arcSize, arcSize);
    Paint arcValue;
    arcValue.setStyle(Paint::Style::STROKE);
    arcValue.setStrokeColor(Color(67, 220, 198));
    arcValue.setStrokeWidth(12.0f);
    arcValue.setStrokeCap(Paint::StrokeCap::ROUND);
    canvas.drawArc(arcBounds, -kPi * 0.85f,
                   kPi * (0.35f + pulse * 1.25f),
                   wsc::Canvas::ArcMode::OPEN, arcValue);

    // Card 4 (SAVE + TRANSFORM): animated rotating rounded rect and axes.
    const RectF transformCard = grid.card(4);
    const PointF transformCenter(transformCard.getX() + grid.cardWidth * 0.5f,
                                 transformCard.getY() + grid.cardHeight * 0.60f);
    canvas.save();
    canvas.translate(transformCenter.getX(), transformCenter.getY());
    canvas.rotate(elapsedSeconds * 0.45f);
    canvas.scale(0.92f + pulse * 0.14f, 0.92f + pulse * 0.14f);
    Paint rotated;
    rotated.setStyle(Paint::Style::FILL);
    rotated.setLinearGradient(-42.0f, -42.0f, 42.0f, 42.0f,
                              Color(97, 121, 246), Color(69, 221, 203));
    const float transformHalfSize = grid.compactCards ? 29.0f : 40.0f;
    canvas.drawRoundRect(RectF(-transformHalfSize, -transformHalfSize,
                               transformHalfSize * 2.0f,
                               transformHalfSize * 2.0f),
                         grid.compactCards ? 11.0f : 15.0f, rotated);
    Paint axis;
    axis.setStyle(Paint::Style::STROKE);
    axis.setStrokeWidth(2.0f);
    axis.setStrokeColor(Color(255, 255, 255, 170));
    const float axisRadius = grid.compactCards ? 38.0f : 54.0f;
    canvas.drawLine(-axisRadius, 0.0f, axisRadius, 0.0f, axis);
    canvas.drawLine(0.0f, -axisRadius, 0.0f, axisRadius, axis);
    canvas.restore();

    // Card 6 (IMAGE + SAMPLING): tiled + rounded image overlay.
    const RectF imageCard = grid.card(6);
    if (checkerImage != nullptr) {
        Paint imagePaint;
        imagePaint.setColor(Color::WHITE);
        imagePaint.setImageSampling(Paint::ImageSampling::NEAREST);
        canvas.drawImageTiled(*checkerImage,
                              RectF(imageCard.getX() + 13.0f,
                                    imageCard.getY() + 40.0f,
                                    grid.cardWidth - 26.0f,
                                    grid.cardHeight - 54.0f),
                              31.0f, 31.0f, imagePaint);
        canvas.drawImageRounded(*checkerImage,
                                RectF(imageCard.getX() + grid.cardWidth - 71.0f,
                                      imageCard.getY() + grid.cardHeight - 74.0f,
                                      56.0f, 56.0f),
                                18.0f, imagePaint);
    }

    // Card 7 (AA + DASH + MOTION): dashed cubic and animated progress bar.
    const RectF motionCard = grid.card(7);
    Path motionPath;
    motionPath.moveTo(motionCard.getX() + 14.0f, motionCard.getY() + 58.0f);
    motionPath.cubicTo(motionCard.getX() + 55.0f, motionCard.getY() + 28.0f,
                       motionCard.getX() + 96.0f, motionCard.getY() + 100.0f,
                       motionCard.getX() + grid.cardWidth - 14.0f,
                       motionCard.getY() + 61.0f);
    Paint dash;
    dash.setStyle(Paint::Style::STROKE);
    dash.setStrokeColor(Color(246, 204, 91));
    dash.setStrokeWidth(4.0f);
    dash.setStrokeCap(Paint::StrokeCap::ROUND);
    dash.setDashPathEffect({10.0f, 7.0f}, -elapsedSeconds * 18.0f);
    dash.setAntiAlias(true);
    canvas.drawPath(motionPath, dash);
    const float trackX = motionCard.getX() + 14.0f;
    const float trackY = motionCard.getY() + grid.cardHeight - 39.0f;
    const float trackWidth = grid.cardWidth - 28.0f;
    Paint progress;
    progress.setStyle(Paint::Style::FILL);
    progress.setLinearGradient(trackX, trackY, trackX + trackWidth, trackY,
                               Color(71, 222, 204), Color(105, 123, 247));
    canvas.drawRoundRect(RectF(trackX, trackY,
                               trackWidth * (0.18f + pulse * 0.78f), 12.0f),
                         6.0f, progress);
}

} // namespace whatscanvas::demo
