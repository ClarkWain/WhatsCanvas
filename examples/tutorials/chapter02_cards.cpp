// Chapter 02 comprehensive example: a small dashboard made from basic shapes.
#include <wsc/wsc.h>

int main()
{
    // 1. 创建离屏画布。示例图片由 Software 后端直接输出。
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 960, 540);
    if (!canvas || !canvas->initializeContext()) return 1;
    canvas->beginFrame();

    // 2. 绘制统一的浅色背景。
    wsc::Paint background;
    background.setColor(wsc::Color(244, 247, 252, 255));
    canvas->drawRect(wsc::RectF(0, 0, 960, 540), background);

    // 3. 每张卡片只改变强调色和进度值，布局保持一致。
    struct Card {
        float x;
        wsc::Color accent;
        float progress;
    };
    const Card cards[] = {
        {60.0f, wsc::Color(73, 109, 232, 255), 0.78f},
        {350.0f, wsc::Color(128, 86, 218, 255), 0.56f},
        {640.0f, wsc::Color(28, 167, 132, 255), 0.88f},
    };

    // 4. 组合圆角矩形、圆形和阴影，构成完整卡片。
    for (const auto &card : cards) {
        const wsc::RectF bounds(card.x, 90, 260, 360);

        // 卡片表面与顶部强调色区域。
        canvas->drawBoxShadow(bounds, 22, 0, 24, 0, 10, wsc::Color(30, 45, 80, 34));

        wsc::Paint surface;
        surface.setColor(wsc::Color(255, 255, 255, 255));
        surface.setAntiAlias(true);
        canvas->drawRoundRect(bounds, 22.0f, surface);

        wsc::Paint header;
        header.setColor(card.accent);
        header.setAntiAlias(true);
        canvas->drawRoundRect(wsc::RectF(card.x, 90, 260, 96), 22.0f, header);
        canvas->drawRect(wsc::RectF(card.x, 150, 260, 36), header);

        // 头像占位和两行标题占位。
        wsc::Paint icon;
        icon.setColor(wsc::Color(255, 255, 255, 52));
        icon.setAntiAlias(true);
        canvas->drawCircle(card.x + 46, 138, 24, icon);
        icon.setStyle(wsc::Paint::Style::STROKE);
        icon.setStrokeWidth(5.0f);
        icon.setColor(wsc::Color(255, 255, 255, 230));
        canvas->drawCircle(card.x + 46, 138, 10, icon);

        wsc::Paint highlight;
        highlight.setColor(wsc::Color(255, 255, 255, 210));
        canvas->drawRoundRect(wsc::RectF(card.x + 84, 122, 116, 12), 6, highlight);
        highlight.setColor(wsc::Color(255, 255, 255, 110));
        canvas->drawRoundRect(wsc::RectF(card.x + 84, 145, 82, 8), 4, highlight);

        // 正文骨架线。
        wsc::Paint skeleton;
        skeleton.setColor(wsc::Color(220, 226, 238, 255));
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 222, 178, 12), 6, skeleton);
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 250, 136, 10), 5, skeleton);
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 278, 194, 10), 5, skeleton);

        // 进度条。
        wsc::Paint track;
        track.setColor(wsc::Color(231, 235, 244, 255));
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 332, 204, 12), 6, track);
        track.setColor(card.accent);
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 332, 204 * card.progress, 12), 6, track);

        // 底部状态标签。
        wsc::Paint badge;
        badge.setColor(wsc::Color(
            card.accent.getR(), card.accent.getG(), card.accent.getB(), 28));
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 382, 86, 30), 15, badge);
        badge.setColor(card.accent);
        canvas->drawCircle(card.x + 46, 397, 5, badge);
        badge.setColor(wsc::Color(190, 198, 214, 255));
        canvas->drawRoundRect(wsc::RectF(card.x + 60, 393, 36, 8), 4, badge);
    }

    // 5. 提交绘制命令并保存与教程对应的结果图。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter02_cards.ppm") ? 0 : 2;
}
