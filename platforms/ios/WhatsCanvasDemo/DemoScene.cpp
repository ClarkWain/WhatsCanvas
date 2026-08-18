#include "DemoScene.h"

#include <wsc/wsc.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace whatscanvas::demo {
namespace {

constexpr float kPi = 3.14159265358979323846f;

wsc::Paint textPaint(float size, const wsc::Color &color,
                     const char *family = "Helvetica Neue")
{
    wsc::Paint paint;
    paint.setStyle(wsc::Paint::Style::FILL);
    paint.setColor(color);
    paint.setTextSize(size);
    paint.setFontFamily(family);
    paint.setTextBaseline(wsc::Paint::TextBaseline::TOP);
    paint.setAntiAlias(true);
    return paint;
}

wsc::Path starPath(float centerX, float centerY, float outerRadius,
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

struct Grid
{
    float width;
    float height;
    int columns;
    float cardWidth;
    float cardHeight;
    float cardTop;
    float cardLeft;

    Grid(float logicalWidth, float logicalHeight,
         float safeTop, float safeBottom, float safeLeft, float safeRight)
        : width(logicalWidth), height(logicalHeight),
          columns(logicalWidth > logicalHeight ? 4 : 2)
    {
        constexpr float margin = 14.0f;
        constexpr float gap = 8.0f;
        const int rows = columns == 4 ? 2 : 4;
        cardTop = safeTop + 69.0f;
        cardLeft = safeLeft + margin;
        cardWidth = (width - safeLeft - safeRight - margin * 2.0f
                     - gap * (columns - 1)) / columns;
        cardHeight = (height - safeTop - safeBottom - 69.0f - 35.0f
                      - gap * (rows - 1)) / rows;
    }

    wsc::RectF card(int index) const
    {
        constexpr float gap = 8.0f;
        return {cardLeft + (index % columns) * (cardWidth + gap),
                cardTop + (index / columns) * (cardHeight + gap),
                cardWidth, cardHeight};
    }
};

void drawCard(wsc::Canvas &canvas, const wsc::RectF &rect,
              const std::string &label)
{
    wsc::Paint fill;
    fill.setStyle(wsc::Paint::Style::FILL);
    fill.setColor(wsc::Color(19, 28, 53, 238));
    canvas.drawRoundRect(rect, 13.0f, fill);

    wsc::Paint border;
    border.setStyle(wsc::Paint::Style::STROKE);
    border.setStrokeColor(wsc::Color(91, 115, 173, 105));
    border.setStrokeWidth(1.0f);
    canvas.drawRoundRect(rect, 13.0f, border);

    auto labelPaint = textPaint(10.0f, wsc::Color(139, 166, 226));
    labelPaint.setLetterSpacing(0.7f);
    canvas.drawText(label, rect.getX() + 11.0f, rect.getY() + 9.0f,
                    labelPaint);
}

void drawStaticScene(wsc::Canvas &canvas, float width, float height,
                     float safeTop, float safeBottom,
                     float safeLeft, float safeRight)
{
    using wsc::Color;
    using wsc::Paint;
    using wsc::PointF;
    using wsc::RectF;

    Paint background;
    background.setStyle(Paint::Style::FILL);
    background.setLinearGradient(0.0f, 0.0f, width, height, {
        {0.0f, Color(8, 12, 29)}, {0.52f, Color(22, 31, 62)},
        {1.0f, Color(19, 12, 43)}});
    canvas.drawRect(RectF(0.0f, 0.0f, width, height), background);

    Paint glow;
    glow.setStyle(Paint::Style::FILL);
    glow.setColor(Color(58, 212, 224, 22));
    canvas.drawCircle(PointF(width * 0.12f, height * 0.2f),
                      std::min(width, height) * 0.38f, glow);
    glow.setColor(Color(150, 86, 232, 20));
    canvas.drawCircle(PointF(width * 0.92f, height * 0.78f),
                      std::min(width, height) * 0.42f, glow);

    auto title = textPaint(23.0f, Color(242, 247, 255));
    title.setFontWeight(500);
    canvas.drawText("WhatsCanvas iOS", safeLeft + 15.0f,
                    safeTop + 13.0f, title);
    auto subtitle = textPaint(10.5f, Color(130, 157, 211));
    canvas.drawText("Metal  |  CoreText  |  60 FPS", safeLeft + 16.0f,
                    safeTop + 44.0f, subtitle);

    Paint pill;
    pill.setStyle(Paint::Style::FILL);
    pill.setColor(Color(43, 204, 164, 42));
    const RectF liveRect(width - safeRight - 66.0f,
                         safeTop + 17.0f, 51.0f, 21.0f);
    canvas.drawRoundRect(liveRect, 10.5f, pill);
    auto live = textPaint(9.0f, Color(83, 235, 196));
    live.setTextAlign(Paint::TextAlign::CENTER);
    canvas.drawText("60 FPS", liveRect.getX() + liveRect.getWidth() * 0.5f,
                    liveRect.getY() + 5.0f, live);

    const Grid grid(width, height, safeTop, safeBottom, safeLeft, safeRight);
    for (int index = 0; index < 8; ++index) {
        static const char *labels[] = {
            "CORETEXT + UTF-8", "PATH + STROKE", "CLIP + GRADIENT",
            "ARCS + ROUND CAPS", "SAVE + TRANSFORM", "SHADOW + BLEND",
            "IMAGE + SAMPLING", "AA + DASH + MOTION"};
        drawCard(canvas, grid.card(index), labels[index]);
    }

    const RectF textCard = grid.card(0);
    auto hero = textPaint(grid.cardHeight < 145.0f ? 22.0f : 27.0f, Color::WHITE);
    hero.setLinearGradient(textCard.getX() + 11.0f, 0.0f,
                           textCard.getX() + grid.cardWidth - 12.0f, 0.0f,
                           Color(75, 225, 207), Color(108, 126, 248));
    canvas.drawText("Aa 123", textCard.getX() + 11.0f,
                    textCard.getY() + 32.0f, hero);
    auto cjk = textPaint(grid.cardHeight < 145.0f ? 12.0f : 15.0f,
                         Color(239, 244, 255), "PingFang SC");
    cjk.setTextLocale("zh-CN");
    canvas.drawText("中文 👩🏽‍💻 🇨🇳 8️⃣", textCard.getX() + 11.0f,
                    textCard.getY() + 69.0f, cjk);
    auto body = textPaint(grid.cardHeight < 145.0f ? 8.5f : 10.0f,
                          Color(155, 178, 218));
    canvas.drawTextBox("Native layout, fallback and cached bitmap raster.",
                       RectF(textCard.getX() + 11.0f, textCard.getY() + 96.0f,
                             grid.cardWidth - 22.0f,
                             std::max(12.0f, grid.cardHeight - 103.0f)),
                       13.0f, 2, true, body);

    const RectF pathCard = grid.card(1);
    auto star = starPath(pathCard.getX() + grid.cardWidth * 0.28f,
                         pathCard.getY() + grid.cardHeight * 0.62f,
                         std::min(38.0f, grid.cardHeight * 0.25f),
                         std::min(17.0f, grid.cardHeight * 0.11f));
    Paint starFill;
    starFill.setStyle(Paint::Style::FILL);
    starFill.setLinearGradient(pathCard.getX(), pathCard.getY(),
                               pathCard.getX() + 90.0f,
                               pathCard.getY() + grid.cardHeight,
                               Color(255, 192, 80), Color(240, 82, 158));
    canvas.drawPath(star, starFill);
    Paint starStroke;
    starStroke.setStyle(Paint::Style::STROKE);
    starStroke.setStrokeColor(Color(255, 238, 192));
    starStroke.setStrokeWidth(2.0f);
    starStroke.setStrokeJoin(Paint::StrokeJoin::ROUND);
    canvas.drawPath(star, starStroke);

    const RectF clipCard = grid.card(2);
    const PointF center(clipCard.getX() + grid.cardWidth * 0.5f,
                        clipCard.getY() + grid.cardHeight * 0.62f);
    auto clipStar = starPath(center.getX(), center.getY(),
                             std::min(58.0f, grid.cardHeight * 0.3f),
                             std::min(29.0f, grid.cardHeight * 0.15f), 7);
    canvas.save();
    canvas.clipPath(clipStar);
    Paint clipped;
    clipped.setStyle(Paint::Style::FILL);
    clipped.setLinearGradient(center.getX() - 60.0f, center.getY() - 55.0f,
                              center.getX() + 60.0f, center.getY() + 55.0f, {
        {0.0f, Color(60, 225, 194)}, {0.48f, Color(63, 148, 237)},
        {1.0f, Color(174, 87, 235)}});
    canvas.drawRect(RectF(center.getX() - 70.0f, center.getY() - 65.0f,
                          140.0f, 130.0f), clipped);
    canvas.restore();

    const RectF arcCard = grid.card(3);
    const float arcSize = std::min(104.0f, grid.cardHeight - 52.0f);
    const RectF arcBounds(arcCard.getX() + (grid.cardWidth - arcSize) * 0.5f,
                          arcCard.getY() + 41.0f, arcSize, arcSize);
    Paint track;
    track.setStyle(Paint::Style::STROKE);
    track.setStrokeColor(Color(49, 64, 96));
    track.setStrokeWidth(12.0f);
    canvas.drawArc(arcBounds, -kPi * 0.85f, kPi * 1.7f,
                   wsc::Canvas::ArcMode::OPEN, track);

    const RectF blendCard = grid.card(5);
    Paint shadow;
    shadow.setStyle(Paint::Style::FILL);
    shadow.setColor(Color(255, 184, 76));
    shadow.setShadowLayer(10.0f, 0.0f, 6.0f, Color(0, 0, 0, 180));
    canvas.drawRoundRect(RectF(blendCard.getX() + 18.0f,
                               blendCard.getY() + 52.0f, 62.0f, 62.0f),
                         18.0f, shadow);
    Paint circle;
    circle.setStyle(Paint::Style::FILL);
    circle.setColor(Color(71, 211, 229, 205));
    canvas.drawCircle(PointF(blendCard.getX() + grid.cardWidth * 0.58f,
                             blendCard.getY() + grid.cardHeight * 0.56f),
                      32.0f, circle);
    circle.setColor(Color(178, 87, 238, 205));
    circle.setBlendMode(Paint::BlendMode::SCREEN);
    canvas.drawCircle(PointF(blendCard.getX() + grid.cardWidth * 0.70f,
                             blendCard.getY() + grid.cardHeight * 0.67f),
                      32.0f, circle);

    auto footer = textPaint(9.5f, Color(108, 132, 183));
    footer.setTextAlign(Paint::TextAlign::CENTER);
    canvas.drawText("8 feature cards  |  native Metal output",
                    (safeLeft + width - safeRight) * 0.5f,
                    height - safeBottom - 23.0f, footer);
}

} // namespace

void createCheckerImage(wsc::Canvas &canvas, std::unique_ptr<wsc::Image> &image)
{
    constexpr int size = 24;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size * size * 4));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool alternate = ((x / 6) + (y / 6)) % 2 != 0;
            const std::size_t offset = static_cast<std::size_t>((y * size + x) * 4);
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
    return canvas.recordPicture([=](wsc::Canvas &recording) {
        drawStaticScene(recording, width, height, safeTop, safeBottom,
                        safeLeft, safeRight);
    });
}

void drawDynamicScene(wsc::Canvas &canvas, const wsc::Image *checkerImage,
                      float width, float height, float safeTop,
                      float safeBottom, float safeLeft, float safeRight,
                      float elapsedSeconds)
{
    using wsc::Color;
    using wsc::Paint;
    using wsc::PointF;
    using wsc::RectF;
    const Grid grid(width, height, safeTop, safeBottom, safeLeft, safeRight);
    const float pulse = 0.5f + 0.5f * std::sin(elapsedSeconds * 2.2f);

    const RectF pathCard = grid.card(1);
    wsc::Path curve;
    curve.moveTo(pathCard.getX() + grid.cardWidth * 0.48f,
                 pathCard.getY() + grid.cardHeight * 0.75f);
    curve.cubicTo(pathCard.getX() + grid.cardWidth * 0.62f,
                  pathCard.getY() + grid.cardHeight * 0.25f,
                  pathCard.getX() + grid.cardWidth * 0.76f,
                  pathCard.getY() + grid.cardHeight * 0.85f,
                  pathCard.getX() + grid.cardWidth - 13.0f,
                  pathCard.getY() + grid.cardHeight * 0.42f);
    Paint dash;
    dash.setStyle(Paint::Style::STROKE);
    dash.setStrokeColor(Color(92, 213, 242));
    dash.setStrokeWidth(4.0f);
    dash.setStrokeCap(Paint::StrokeCap::ROUND);
    dash.setDashPathEffect({9.0f, 6.0f}, elapsedSeconds * 12.0f);
    canvas.drawPath(curve, dash);

    const RectF arcCard = grid.card(3);
    const float arcSize = std::min(104.0f, grid.cardHeight - 52.0f);
    const RectF arcBounds(arcCard.getX() + (grid.cardWidth - arcSize) * 0.5f,
                          arcCard.getY() + 41.0f, arcSize, arcSize);
    Paint arc;
    arc.setStyle(Paint::Style::STROKE);
    arc.setStrokeColor(Color(67, 220, 198));
    arc.setStrokeWidth(12.0f);
    arc.setStrokeCap(Paint::StrokeCap::ROUND);
    canvas.drawArc(arcBounds, -kPi * 0.85f,
                   kPi * (0.35f + pulse * 1.25f),
                   wsc::Canvas::ArcMode::OPEN, arc);

    const RectF transformCard = grid.card(4);
    canvas.save();
    canvas.translate(transformCard.getX() + grid.cardWidth * 0.5f,
                     transformCard.getY() + grid.cardHeight * 0.61f);
    canvas.rotate(elapsedSeconds * 0.45f);
    canvas.scale(0.92f + pulse * 0.14f, 0.92f + pulse * 0.14f);
    Paint rotated;
    rotated.setStyle(Paint::Style::FILL);
    rotated.setLinearGradient(-42.0f, -42.0f, 42.0f, 42.0f,
                              Color(97, 121, 246), Color(69, 221, 203));
    const float half = grid.cardHeight < 145.0f ? 29.0f : 40.0f;
    canvas.drawRoundRect(RectF(-half, -half, half * 2.0f, half * 2.0f),
                         14.0f, rotated);
    canvas.restore();

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
    }

    const RectF motionCard = grid.card(7);
    const float trackX = motionCard.getX() + 14.0f;
    const float trackY = motionCard.getY() + grid.cardHeight - 39.0f;
    const float trackWidth = grid.cardWidth - 28.0f;
    Paint track;
    track.setStyle(Paint::Style::FILL);
    track.setColor(Color(7, 12, 28, 230));
    canvas.drawRoundRect(RectF(trackX, trackY, trackWidth, 12.0f), 6.0f, track);
    Paint progress;
    progress.setStyle(Paint::Style::FILL);
    progress.setLinearGradient(trackX, trackY, trackX + trackWidth, trackY,
                               Color(71, 222, 204), Color(105, 123, 247));
    canvas.drawRoundRect(RectF(trackX, trackY,
                              trackWidth * (0.18f + pulse * 0.78f), 12.0f),
                         6.0f, progress);
}

} // namespace whatscanvas::demo
