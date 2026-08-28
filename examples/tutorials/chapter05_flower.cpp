// Chapter 05 comprehensive example: a flower composed with save/restore transforms.
#include <wsc/wsc.h>

#include <cmath>

int main()
{
    // 1. 创建离屏画布并定义花朵的共享几何数据。
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 720, 720);
    if (!canvas || !canvas->initializeContext()) return 1;

    constexpr float pi = 3.14159265f, cx = 360.0f, cy = 350.0f;
    constexpr int petalCount = 10;
    const wsc::Color colors[] = {
        wsc::Color(255, 112, 132, 238), wsc::Color(255, 139, 113, 238),
        wsc::Color(250, 169, 92, 238), wsc::Color(241, 114, 151, 238),
        wsc::Color(216, 103, 169, 238), wsc::Color(180, 104, 190, 238),
        wsc::Color(139, 111, 204, 238), wsc::Color(111, 130, 215, 238),
        wsc::Color(91, 151, 217, 238), wsc::Color(91, 177, 197, 238),
    };

    // 2. 绘制背景和两条辅助圆，辅助圆用于说明旋转半径。
    canvas->beginFrame();
    wsc::Paint background;
    background.setLinearGradient(0, 0, 720, 720,
        wsc::Color(252, 248, 246, 255), wsc::Color(238, 243, 251, 255));
    canvas->drawRect(wsc::RectF(0, 0, 720, 720), background);

    wsc::Paint guide;
    guide.setStyle(wsc::Paint::Style::STROKE);
    guide.setStrokeWidth(2.0f);
    guide.setColor(wsc::Color(142, 157, 184, 34));
    guide.setAntiAlias(true);
    canvas->drawCircle(cx, cy, 205, guide);
    canvas->drawCircle(cx, cy, 92, guide);

    // 3. 先绘制位于花瓣下方的茎和叶子。
    wsc::Paint stem;
    stem.setStyle(wsc::Paint::Style::STROKE);
    stem.setStrokeWidth(18.0f);
    stem.setStrokeCap(wsc::Paint::StrokeCap::ROUND);
    stem.setColor(wsc::Color(72, 153, 116, 255));
    stem.setAntiAlias(true);
    canvas->drawLine(cx, cy + 48.0f, cx, 650.0f, stem);

    wsc::Paint leaf;
    leaf.setColor(wsc::Color(86, 177, 132, 235));
    leaf.setAntiAlias(true);
    canvas->save();
    canvas->translate(cx - 8, 535);
    canvas->rotate(-0.78f);
    canvas->drawOval(wsc::RectF(-20, -12, 110, 48), leaf);
    canvas->restore();

    canvas->save();
    canvas->translate(cx + 6, 585);
    canvas->rotate(0.72f);
    leaf.setColor(wsc::Color(58, 149, 107, 230));
    canvas->drawOval(wsc::RectF(-85, -12, 110, 48), leaf);
    canvas->restore();

    // 4. 每片花瓣都在相同局部坐标中绘制，只改变画布旋转角度。
    for (int i = 0; i < petalCount; ++i) {
        canvas->save();
        canvas->translate(cx, cy);
        canvas->rotate(i * 2.0f * pi / petalCount);
        wsc::Paint petal;
        petal.setColor(colors[i]);
        petal.setAntiAlias(true);
        petal.setShadowLayer(12, 0, 8, wsc::Color(79, 58, 92, 34));
        canvas->drawOval(wsc::RectF(-40, -226, 80, 172), petal);
        canvas->restore();
    }

    // 5. 中心圆最后绘制，用来遮住花瓣交叠处。
    wsc::Paint center;
    center.setRadialGradient(cx - 20, cy - 20, 78,
        wsc::Color(255, 232, 104, 255), wsc::Color(240, 176, 52, 255));
    center.setAntiAlias(true);
    center.setShadowLayer(14, 0, 6, wsc::Color(120, 78, 20, 50));
    canvas->drawCircle(cx, cy, 66, center);
    wsc::Paint seed;
    seed.setColor(wsc::Color(196, 125, 42, 115));
    for (int i = 0; i < 12; ++i) {
        const float a = i * 2.0f * pi / 12.0f;
        canvas->drawCircle(cx + std::cos(a) * 34, cy + std::sin(a) * 34, 4, seed);
    }

    // 6. 提交并保存结果图。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter05_flower.ppm") ? 0 : 2;
}
