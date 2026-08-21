#include <wsc/FontSystem.h>

#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

#include <wsc/wsc.h>

namespace whatscanvas::scenes {

enum class StressSceneId
{
    Text,
    Geometry,
    Compositing
};

inline constexpr const char* stressSceneName(StressSceneId id)
{
    switch (id) {
    case StressSceneId::Text: return "text_stress";
    case StressSceneId::Geometry: return "geometry_stress";
    case StressSceneId::Compositing: return "compositing_stress";
    }
    return "text_stress";
}

inline bool parseStressScene(std::string_view name, StressSceneId& id)
{
    if (name == "text_stress") {
        id = StressSceneId::Text;
        return true;
    }
    if (name == "geometry_stress") {
        id = StressSceneId::Geometry;
        return true;
    }
    if (name == "compositing_stress") {
        id = StressSceneId::Compositing;
        return true;
    }
    return false;
}

namespace stress_detail {

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr const char* kPrimaryFamily =
    wsc::FontSystem::kDefaultPrimaryFamily;
inline constexpr const char* kCjkFamily =
    wsc::FontSystem::kDefaultCjkFamily;

inline wsc::Paint textPaint(float size, const wsc::Color& color,
                            const char* family = kPrimaryFamily)
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

inline wsc::Path star(float cx, float cy, float outer, float inner,
                      int points = 7)
{
    wsc::Path path;
    for (int i = 0; i < points * 2; ++i) {
        const float radius = i % 2 == 0 ? outer : inner;
        const float angle = -kPi * 0.5f
            + static_cast<float>(i) * kPi / static_cast<float>(points);
        const float x = cx + std::cos(angle) * radius;
        const float y = cy + std::sin(angle) * radius;
        if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
    }
    path.close();
    return path;
}

inline void background(wsc::Canvas& canvas, float width, float height,
                       const wsc::Color& glow)
{
    wsc::Paint paint;
    paint.setLinearGradient(0.0f, 0.0f, width, height, {
        {0.0f, wsc::Color(7, 11, 27)},
        {0.55f, wsc::Color(20, 29, 57)},
        {1.0f, wsc::Color(16, 10, 38)}
    });
    canvas.drawRect(wsc::RectF(0.0f, 0.0f, width, height), paint);
    paint.clearShader();
    paint.setColor(glow);
    canvas.drawCircle(wsc::PointF(width * 0.88f, height * 0.16f),
                      std::min(width, height) * 0.35f, paint);
}

inline void heading(wsc::Canvas& canvas, float width, const char* title,
                    const char* subtitle, const wsc::Color& accent)
{
    auto titlePaint = textPaint(24.0f, wsc::Color(244, 248, 255));
    canvas.drawText(title, 18.0f, 14.0f, titlePaint);
    auto subtitlePaint = textPaint(10.5f, wsc::Color(139, 163, 211));
    canvas.drawText(subtitle, 19.0f, 45.0f, subtitlePaint);
    wsc::Paint marker;
    marker.setColor(accent);
    canvas.drawRoundRect(wsc::RectF(width - 52.0f, 18.0f, 34.0f, 8.0f),
                         4.0f, marker);
}

inline void panel(wsc::Canvas& canvas, const wsc::RectF& rect,
                  const char* label)
{
    wsc::Paint fill;
    fill.setColor(wsc::Color(18, 28, 54, 235));
    canvas.drawRoundRect(rect, 14.0f, fill);
    wsc::Paint border;
    border.setStyle(wsc::Paint::Style::STROKE);
    border.setStrokeColor(wsc::Color(103, 126, 181, 112));
    border.setStrokeWidth(1.0f);
    canvas.drawRoundRect(rect, 14.0f, border);
    auto labelPaint = textPaint(9.0f, wsc::Color(124, 151, 209));
    labelPaint.setLetterSpacing(0.8f);
    canvas.drawText(label, rect.getX() + 11.0f, rect.getY() + 9.0f,
                    labelPaint);
}

struct Grid
{
    float x = 14.0f;
    float y = 70.0f;
    float gap = 9.0f;
    float cellWidth = 0.0f;
    float cellHeight = 0.0f;
    int columns = 1;

    wsc::RectF cell(int index) const
    {
        return wsc::RectF(
            x + static_cast<float>(index % columns) * (cellWidth + gap),
            y + static_cast<float>(index / columns) * (cellHeight + gap),
            cellWidth, cellHeight);
    }
};

inline Grid grid(float width, float height, int itemCount)
{
    const bool landscape = width > height;
    Grid result;
    result.columns = landscape ? itemCount : 2;
    const int rows = (itemCount + result.columns - 1) / result.columns;
    result.cellWidth = (width - 28.0f
        - result.gap * static_cast<float>(result.columns - 1))
        / static_cast<float>(result.columns);
    result.cellHeight = (height - result.y - 16.0f
        - result.gap * static_cast<float>(rows - 1))
        / static_cast<float>(rows);
    return result;
}

inline void drawTextScene(wsc::Canvas& canvas, float width, float height,
                          float elapsed)
{
    background(canvas, width, height, wsc::Color(45, 205, 226, 22));
    heading(canvas, width, "Text stress matrix",
            "fallback  |  shaping  |  layout  |  glyph effects",
            wsc::Color(64, 222, 202));
    const Grid layout = grid(width, height, 4);

    const wsc::RectF mixed = layout.cell(0);
    panel(canvas, mixed, "MIXED SCRIPT + FALLBACK");
    auto hero = textPaint(std::min(30.0f, mixed.getHeight() * 0.18f),
                          wsc::Color::WHITE, kCjkFamily);
    hero.setTextLocale("zh-CN");
    hero.setLinearGradient(mixed.getX() + 12.0f, 0.0f,
                           mixed.getX() + mixed.getWidth() - 12.0f, 0.0f,
                           wsc::Color(69, 229, 202), wsc::Color(125, 113, 246));
    canvas.drawText("Aa 中日 한글 123", mixed.getX() + 12.0f,
                    mixed.getY() + 34.0f, hero);
    auto emoji = textPaint(std::min(21.0f, mixed.getHeight() * 0.13f),
                           wsc::Color(234, 241, 255), kCjkFamily);
    emoji.setTextLocale("zh-CN");
    canvas.drawText("中文 👩🏽‍💻 🇨🇳 8️⃣  é  ﬁ", mixed.getX() + 12.0f,
                    mixed.getY() + 76.0f, emoji);
    auto metrics = textPaint(10.5f, wsc::Color(145, 170, 216));
    canvas.drawTextBox(
        "Fallback must preserve cluster order, combining marks and emoji sequences.",
        wsc::RectF(mixed.getX() + 12.0f, mixed.getY() + 108.0f,
                   mixed.getWidth() - 24.0f,
                   std::max(16.0f, mixed.getHeight() - 118.0f)),
        14.0f, 3, true, metrics);

    const wsc::RectF wrapping = layout.cell(1);
    panel(canvas, wrapping, "WRAP + ALIGN + ELLIPSIS");
    auto body = textPaint(11.0f, wsc::Color(222, 232, 250));
    const float columnWidth = (wrapping.getWidth() - 32.0f) * 0.5f;
    canvas.drawTextBox(
        "A narrow text box validates word wrapping, line height and deterministic ellipsis.",
        wsc::RectF(wrapping.getX() + 11.0f, wrapping.getY() + 34.0f,
                   columnWidth, wrapping.getHeight() - 44.0f),
        14.0f, 5, true, body);
    body.setTextAlign(wsc::Paint::TextAlign::RIGHT);
    canvas.drawTextBox(
        "Right aligned text must keep the same edge on every renderer.",
        wsc::RectF(wrapping.getX() + 21.0f + columnWidth,
                   wrapping.getY() + 34.0f, columnWidth,
                   wrapping.getHeight() - 44.0f),
        14.0f, 4, true, body);

    const wsc::RectF baselines = layout.cell(2);
    panel(canvas, baselines, "BASELINE + STROKE + SHADOW");
    const float centerX = baselines.getX() + baselines.getWidth() * 0.5f;
    const float centerY = baselines.getY() + baselines.getHeight() * 0.56f;
    wsc::Paint guides;
    guides.setStyle(wsc::Paint::Style::STROKE);
    guides.setStrokeColor(wsc::Color(97, 133, 196, 125));
    guides.setStrokeWidth(1.0f);
    canvas.drawLine(baselines.getX() + 12.0f, centerY,
                    baselines.getX() + baselines.getWidth() - 12.0f, centerY,
                    guides);
    canvas.drawLine(centerX, baselines.getY() + 30.0f, centerX,
                    baselines.getY() + baselines.getHeight() - 12.0f, guides);
    auto outlined = textPaint(std::min(31.0f, baselines.getHeight() * 0.24f),
                              wsc::Color(62, 220, 199));
    outlined.setStyle(wsc::Paint::Style::FILL_AND_STROKE);
    outlined.setStrokeColor(wsc::Color(239, 248, 255));
    outlined.setStrokeWidth(1.2f);
    outlined.setTextAlign(wsc::Paint::TextAlign::CENTER);
    outlined.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    outlined.setShadowLayer(7.0f, 0.0f, 4.0f, wsc::Color(0, 0, 0, 180));
    canvas.drawText("Middle", centerX, centerY, outlined);

    const wsc::RectF pathCell = layout.cell(3);
    panel(canvas, pathCell, "TEXT ON PATH + TRANSFORM");
    wsc::Path wave;
    const float left = pathCell.getX() + 13.0f;
    const float right = pathCell.getX() + pathCell.getWidth() - 13.0f;
    const float waveY = pathCell.getY() + pathCell.getHeight() * 0.58f;
    wave.moveTo(left, waveY);
    wave.cubicTo(left + (right - left) * 0.25f, waveY - 48.0f,
                 left + (right - left) * 0.70f, waveY + 48.0f,
                 right, waveY - 8.0f);
    wsc::Paint wavePaint;
    wavePaint.setStyle(wsc::Paint::Style::STROKE);
    wavePaint.setStrokeColor(wsc::Color(89, 154, 239, 150));
    wavePaint.setStrokeWidth(2.0f);
    canvas.drawPath(wave, wavePaint);
    auto pathText = textPaint(12.0f, wsc::Color(250, 213, 100));
    pathText.setLetterSpacing(0.6f);
    canvas.drawTextOnPath("PATH TEXT  •  GLYPH METRICS  •  ", wave,
                          std::fmod(elapsed * 16.0f, 38.0f), -12.0f, pathText);
    canvas.save();
    canvas.translate(pathCell.getX() + pathCell.getWidth() * 0.5f,
                     pathCell.getY() + pathCell.getHeight() - 31.0f);
    canvas.rotate(-0.10f);
    auto spaced = textPaint(10.0f, wsc::Color(171, 190, 228));
    spaced.setTextAlign(wsc::Paint::TextAlign::CENTER);
    spaced.setLetterSpacing(2.1f);
    canvas.drawText("LETTER SPACING", 0.0f, 0.0f, spaced);
    canvas.restore();
}

inline void drawGeometryScene(wsc::Canvas& canvas, float width, float height,
                              float elapsed)
{
    background(canvas, width, height, wsc::Color(120, 91, 241, 22));
    heading(canvas, width, "Geometry stress matrix",
            "fill rules  |  nested clips  |  joins  |  precision",
            wsc::Color(147, 103, 247));
    const Grid layout = grid(width, height, 4);

    const wsc::RectF fillCell = layout.cell(0);
    panel(canvas, fillCell, "EVEN-ODD + CONCAVE FILL");
    wsc::Path rings;
    rings.setFillType(wsc::Path::FillType::EVEN_ODD);
    const float ringX = fillCell.getX() + fillCell.getWidth() * 0.34f;
    const float ringY = fillCell.getY() + fillCell.getHeight() * 0.58f;
    const float ringRadius = std::min(fillCell.getWidth(), fillCell.getHeight()) * 0.24f;
    rings.addCircle(ringX, ringY, ringRadius);
    rings.addCircle(ringX, ringY, ringRadius * 0.55f);
    wsc::Paint fill;
    fill.setLinearGradient(ringX - ringRadius, ringY - ringRadius,
                           ringX + ringRadius, ringY + ringRadius,
                           wsc::Color(250, 191, 74), wsc::Color(239, 75, 154));
    canvas.drawPath(rings, fill);
    wsc::Path concave = star(fillCell.getX() + fillCell.getWidth() * 0.75f,
                             ringY, ringRadius, ringRadius * 0.37f, 9);
    fill.setLinearGradient(0.0f, ringY - ringRadius, 0.0f, ringY + ringRadius,
                           wsc::Color(64, 224, 200), wsc::Color(68, 126, 239));
    canvas.drawPath(concave, fill);

    const wsc::RectF clipCell = layout.cell(1);
    panel(canvas, clipCell, "NESTED NON-RECT CLIP");
    const float clipX = clipCell.getX() + clipCell.getWidth() * 0.5f;
    const float clipY = clipCell.getY() + clipCell.getHeight() * 0.59f;
    const float clipR = std::min(clipCell.getWidth(), clipCell.getHeight()) * 0.31f;
    canvas.save();
    wsc::Path outer = star(clipX, clipY, clipR, clipR * 0.58f, 7);
    canvas.clipPath(outer);
    wsc::Path inner;
    inner.addOval(wsc::RectF(clipX - clipR * 0.72f, clipY - clipR,
                             clipR * 1.44f, clipR * 2.0f));
    canvas.clipPath(inner);
    wsc::Paint stripes;
    stripes.setStrokeWidth(9.0f);
    stripes.setStrokeColor(wsc::Color(101, 218, 237));
    for (int i = -6; i <= 6; ++i) {
        const float dx = static_cast<float>(i) * 18.0f;
        canvas.drawLine(clipX - clipR + dx, clipY + clipR,
                        clipX + dx, clipY - clipR, stripes);
    }
    canvas.restore();

    const wsc::RectF strokeCell = layout.cell(2);
    panel(canvas, strokeCell, "MITER + CAPS + DASH PHASE");
    const float sx = strokeCell.getX() + 16.0f;
    const float sy = strokeCell.getY() + strokeCell.getHeight() * 0.70f;
    wsc::Path zigzag;
    zigzag.moveTo(sx, sy);
    zigzag.lineTo(sx + strokeCell.getWidth() * 0.20f, sy - 70.0f);
    zigzag.lineTo(sx + strokeCell.getWidth() * 0.34f, sy - 5.0f);
    zigzag.lineTo(sx + strokeCell.getWidth() * 0.55f, sy - 60.0f);
    zigzag.lineTo(strokeCell.getX() + strokeCell.getWidth() - 14.0f, sy - 12.0f);
    wsc::Paint miter;
    miter.setStyle(wsc::Paint::Style::STROKE);
    miter.setStrokeColor(wsc::Color(248, 209, 91));
    miter.setStrokeWidth(8.0f);
    miter.setStrokeJoin(wsc::Paint::StrokeJoin::MITER);
    miter.setStrokeMiterLimit(2.0f);
    miter.setStrokeCap(wsc::Paint::StrokeCap::SQUARE);
    miter.setDashPathEffect({18.0f, 7.0f, 4.0f, 7.0f}, elapsed * 22.0f);
    canvas.drawPath(zigzag, miter);

    const wsc::RectF precisionCell = layout.cell(3);
    panel(canvas, precisionCell, "SUBPIXEL + ARC + NEGATIVE SCALE");
    const float px = precisionCell.getX() + precisionCell.getWidth() * 0.5f;
    const float py = precisionCell.getY() + precisionCell.getHeight() * 0.58f;
    wsc::Paint hairline;
    hairline.setStyle(wsc::Paint::Style::STROKE);
    hairline.setStrokeColor(wsc::Color(132, 162, 224, 170));
    hairline.setStrokeWidth(0.75f);
    for (int i = -4; i <= 4; ++i) {
        const float offset = static_cast<float>(i) * 8.25f;
        canvas.drawLine(px - 54.5f, py + offset, px + 54.5f, py + offset,
                        hairline);
    }
    wsc::Paint arc;
    arc.setStyle(wsc::Paint::Style::STROKE);
    arc.setStrokeColor(wsc::Color(66, 224, 199));
    arc.setStrokeWidth(7.0f);
    arc.setStrokeCap(wsc::Paint::StrokeCap::ROUND);
    canvas.drawArc(wsc::RectF(px - 43.0f, py - 43.0f, 86.0f, 86.0f),
                   -2.2f, 4.3f, wsc::Canvas::ArcMode::OPEN, arc);
    canvas.save();
    canvas.translate(px, py);
    canvas.scale(-0.72f, 0.72f);
    canvas.rotate(0.37f);
    wsc::Paint marker;
    marker.setColor(wsc::Color(238, 91, 161, 190));
    canvas.drawPath(star(0.0f, 0.0f, 29.0f, 12.0f, 5), marker);
    canvas.restore();
}

inline void drawCompositingScene(wsc::Canvas& canvas, float width,
                                 float height, float elapsed)
{
    background(canvas, width, height, wsc::Color(238, 91, 161, 18));
    heading(canvas, width, "Compositing stress matrix",
            "saveLayer  |  blend modes  |  blur  |  alpha",
            wsc::Color(244, 103, 164));
    const Grid layout = grid(width, height, 4);

    for (int index = 0; index < 4; ++index) {
        const wsc::RectF cell = layout.cell(index);
        panel(canvas, cell, index == 0 ? "PORTER-DUFF CUTOUT"
                     : index == 1 ? "MULTIPLY + SCREEN"
                     : index == 2 ? "BACKDROP FROSTED GLASS"
                                  : "INNER SHADOW + LAYER ALPHA");
        wsc::Paint tile;
        const float tileSize = 18.0f;
        for (float y = cell.getY() + 29.0f;
             y < cell.getY() + cell.getHeight() - 8.0f; y += tileSize) {
            for (float x = cell.getX() + 8.0f;
                 x < cell.getX() + cell.getWidth() - 8.0f; x += tileSize) {
                const int parity = static_cast<int>((x + y) / tileSize) & 1;
                tile.setColor(parity ? wsc::Color(42, 64, 103, 190)
                                     : wsc::Color(24, 39, 75, 190));
                canvas.drawRect(wsc::RectF(x, y, tileSize, tileSize), tile);
            }
        }
    }

    const wsc::RectF cutout = layout.cell(0);
    wsc::Paint layerPaint;
    layerPaint.setColor(wsc::Color::WHITE);
    canvas.saveLayer(cutout, layerPaint);
    wsc::Paint shape;
    shape.setLinearGradient(cutout.getX(), cutout.getY(),
                            cutout.getX() + cutout.getWidth(),
                            cutout.getY() + cutout.getHeight(),
                            wsc::Color(66, 222, 201), wsc::Color(112, 106, 244));
    canvas.drawRoundRect(wsc::RectF(cutout.getX() + 24.0f,
                                    cutout.getY() + 49.0f,
                                    cutout.getWidth() - 48.0f,
                                    cutout.getHeight() - 72.0f), 24.0f, shape);
    wsc::Paint erase;
    erase.setColor(wsc::Color::WHITE);
    erase.setBlendMode(wsc::Paint::BlendMode::DST_OUT);
    canvas.drawCircle(wsc::PointF(cutout.getX() + cutout.getWidth() * 0.55f,
                                  cutout.getY() + cutout.getHeight() * 0.62f),
                      std::min(cutout.getWidth(), cutout.getHeight()) * 0.18f,
                      erase);
    canvas.restore();

    const wsc::RectF blends = layout.cell(1);
    const float bx = blends.getX() + blends.getWidth() * 0.5f;
    const float by = blends.getY() + blends.getHeight() * 0.62f;
    const float br = std::min(blends.getWidth(), blends.getHeight()) * 0.22f;
    wsc::Paint a;
    a.setColor(wsc::Color(250, 183, 65, 220));
    a.setBlendMode(wsc::Paint::BlendMode::MULTIPLY);
    canvas.drawCircle(wsc::PointF(bx - br * 0.55f, by), br, a);
    wsc::Paint b;
    b.setColor(wsc::Color(74, 212, 232, 210));
    b.setBlendMode(wsc::Paint::BlendMode::SCREEN);
    canvas.drawCircle(wsc::PointF(bx + br * 0.55f, by), br, b);
    wsc::Paint add;
    add.setColor(wsc::Color(216, 76, 177, 155));
    add.setBlendMode(wsc::Paint::BlendMode::ADD);
    canvas.drawCircle(wsc::PointF(bx, by - br * 0.58f), br, add);

    const wsc::RectF glass = layout.cell(2);
    const float drift = std::sin(elapsed * 1.7f) * 9.0f;
    wsc::LayerOptions glassOptions;
    glassOptions.setBackdropFilter(
        wsc::ImageFilter::frostedGlass(4.0f, 1.06f, 1.03f, 1.0f, 0.0f));
    const wsc::RectF glassRect(glass.getX() + 21.0f + drift,
                               glass.getY() + 47.0f,
                               glass.getWidth() - 42.0f,
                               glass.getHeight() - 69.0f);
    canvas.saveLayer(glassRect, layerPaint, glassOptions);
    wsc::Paint tint;
    tint.setColor(wsc::Color(226, 239, 255, 70));
    canvas.drawRoundRect(glassRect, 20.0f, tint);
    canvas.restore();

    const wsc::RectF inset = layout.cell(3);
    wsc::LayerOptions insetOptions;
    insetOptions.setImageFilter(wsc::ImageFilter::innerShadow(
        7.0f, 4.0f, 2.0f, 3.0f, wsc::Color(2, 8, 24, 210)));
    wsc::Paint fadedLayer;
    fadedLayer.setAlpha(0.82f);
    const wsc::RectF insetRect(inset.getX() + 22.0f, inset.getY() + 48.0f,
                               inset.getWidth() - 44.0f,
                               inset.getHeight() - 70.0f);
    canvas.saveLayer(insetRect, fadedLayer, insetOptions);
    wsc::Paint control;
    control.setLinearGradient(insetRect.getX(), insetRect.getY(),
                              insetRect.getX(),
                              insetRect.getY() + insetRect.getHeight(),
                              wsc::Color(235, 242, 251, 245),
                              wsc::Color(143, 168, 208, 230));
    canvas.drawRoundRect(insetRect, 18.0f, control);
    canvas.restore();
}

} // namespace stress_detail

inline void drawStressScene(wsc::Canvas& canvas, StressSceneId id,
                            float width, float height, float elapsedSeconds)
{
    switch (id) {
    case StressSceneId::Text:
        stress_detail::drawTextScene(canvas, width, height, elapsedSeconds);
        break;
    case StressSceneId::Geometry:
        stress_detail::drawGeometryScene(canvas, width, height, elapsedSeconds);
        break;
    case StressSceneId::Compositing:
        stress_detail::drawCompositingScene(canvas, width, height, elapsedSeconds);
        break;
    }
}

} // namespace whatscanvas::scenes
