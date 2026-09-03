// Chapter 04 comprehensive example: a readable gauge built with paths and arcs.
#include <wsc/FontSystem.h>
#include <wsc/wsc.h>

#include <cmath>

int main()
{
    // 1. 创建画布并准备文字渲染。
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 720, 720);
    if (!canvas || !canvas->initializeContext()) return 1;
    for (const auto &face : wsc::FontSystem::defaultSystemFontFaces()) canvas->registerFontFace(face);
    canvas->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());

    constexpr float pi = 3.14159265f, cx = 360.0f, cy = 350.0f, radius = 250.0f;
    constexpr float progress = 0.70f, start = pi * 0.75f, sweep = pi * 1.5f;

    // 2. 绘制深色背景和仪表盘光晕。
    canvas->beginFrame();

    wsc::Paint background;
    background.setLinearGradient(0, 0, 720, 720,
        wsc::Color(20, 25, 42, 255), wsc::Color(10, 14, 27, 255));
    canvas->drawRect(wsc::RectF(0, 0, 720, 720), background);

    wsc::Paint halo;
    halo.setColor(wsc::Color(82, 113, 255, 18));
    halo.setAntiAlias(true);
    canvas->drawCircle(cx, cy, radius + 52, halo);

    // 3. 将 270 度量程等分成 20 段，每五格使用一个长刻度。
    wsc::Paint tick;
    tick.setStyle(wsc::Paint::Style::STROKE);
    tick.setStrokeCap(wsc::Paint::StrokeCap::ROUND);
    tick.setAntiAlias(true);
    for (int i = 0; i <= 20; ++i) {
        const float t = static_cast<float>(i) / 20.0f;
        const float angle = start + sweep * t;
        const float outer = radius - 27;
        const float inner = outer - (i % 5 == 0 ? 22.0f : 12.0f);
        tick.setStrokeWidth(i % 5 == 0 ? 4.0f : 2.0f);
        tick.setColor(i <= 14 ? wsc::Color(117, 145, 255, 210) : wsc::Color(82, 91, 116, 180));
        canvas->drawLine(cx + std::cos(angle) * inner, cy + std::sin(angle) * inner,
                         cx + std::cos(angle) * outer, cy + std::sin(angle) * outer, tick);
    }

    // 4. 先绘制完整轨道，再按 progress 覆盖前景进度弧。
    const wsc::RectF arcBounds(cx - radius, cy - radius, radius * 2, radius * 2);
    wsc::Paint track;
    track.setStyle(wsc::Paint::Style::STROKE);
    track.setStrokeWidth(24.0f);
    track.setStrokeCap(wsc::Paint::StrokeCap::ROUND);
    track.setColor(wsc::Color(54, 63, 88, 255));
    track.setAntiAlias(true);
    canvas->drawArc(arcBounds, start, sweep, wsc::Canvas::ArcMode::OPEN, track);

    wsc::Paint valueArc = track;
    valueArc.setStrokeWidth(26.0f);
    valueArc.setColor(wsc::Color(92, 126, 255, 255));
    valueArc.setShadowLayer(18, 0, 0, wsc::Color(74, 111, 255, 90));
    canvas->drawArc(arcBounds, start, sweep * progress, wsc::Canvas::ArcMode::OPEN, valueArc);

    // 5. 指针与进度弧使用相同角度公式，保证两者指向一致。
    const float angle = start + sweep * progress;
    wsc::Paint needle;
    needle.setStyle(wsc::Paint::Style::STROKE);
    needle.setStrokeWidth(7.0f);
    needle.setStrokeCap(wsc::Paint::StrokeCap::ROUND);
    needle.setColor(wsc::Color(244, 247, 255, 255));
    needle.setAntiAlias(true);
    canvas->drawLine(cx, cy, cx + std::cos(angle) * 152, cy + std::sin(angle) * 152, needle);

    wsc::Paint hub;
    hub.setColor(wsc::Color(92, 126, 255, 255));
    hub.setAntiAlias(true);
    hub.setShadowLayer(14, 0, 0, wsc::Color(74, 111, 255, 110));
    canvas->drawCircle(cx, cy, 18, hub);
    hub.setColor(wsc::Color(244, 247, 255, 255));
    canvas->drawCircle(cx, cy, 7, hub);

    // 6. 数值和标签使用独立纵向位置，避免文字重叠。
    wsc::Paint value;
    value.setColor(wsc::Color(247, 249, 255, 255));
    value.setTextSize(66.0f);
    value.setFontWeight(650);
    value.setTextAlign(wsc::Paint::TextAlign::CENTER);
    value.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    canvas->drawText("70%", cx, 500, value);
    wsc::Paint label = value;
    label.setColor(wsc::Color(139, 151, 181, 255));
    label.setTextSize(16.0f);
    label.setFontWeight(500);
    canvas->drawText("RENDER PERFORMANCE", cx, 550, label);

    // 7. 输出教程中的仪表盘图片。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter04_gauge.ppm") ? 0 : 2;
}
