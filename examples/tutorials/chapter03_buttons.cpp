// Chapter 03 comprehensive example: gradient buttons and Paint states.
#include <wsc/FontSystem.h>
#include <wsc/wsc.h>

int main()
{
    // 1. 输出 960x480 物理像素，使用 480x240 的逻辑坐标布局。
    // 文档按 480 像素宽展示图片，因此 DPR=2 可以保留清晰边缘。
    constexpr int kPhysicalWidth = 960;
    constexpr int kPhysicalHeight = 480;
    constexpr float kDpr = 2.0f;
    constexpr float kLogicalWidth = kPhysicalWidth / kDpr;
    constexpr float kLogicalHeight = kPhysicalHeight / kDpr;

    auto canvas = wsc::Canvas::create(
        wsc::Canvas::Backend::Software, kPhysicalWidth, kPhysicalHeight);
    if (!canvas) return 1;
    canvas->setDevicePixelRatio(kDpr);
    if (!canvas->initializeContext()) return 1;
    for (const auto &face : wsc::FontSystem::defaultSystemFontFaces()) canvas->registerFontFace(face);
    canvas->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());

    // 2. 绘制页面背景和标题。
    canvas->beginFrame();
    wsc::Paint background;
    background.setLinearGradient(0, 0, kLogicalWidth, kLogicalHeight,
        wsc::Color(247, 249, 253, 255), wsc::Color(236, 241, 250, 255));
    canvas->drawRect(wsc::RectF(0, 0, kLogicalWidth, kLogicalHeight), background);

    wsc::Paint title;
    title.setColor(wsc::Color(28, 37, 58, 255));
    title.setTextSize(22.0f);
    title.setFontWeight(650);
    title.setTextAlign(wsc::Paint::TextAlign::CENTER);
    title.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    canvas->drawText("Paint states", kLogicalWidth / 2, 35, title);

    wsc::Paint subtitle = title;
    subtitle.setColor(wsc::Color(105, 116, 139, 255));
    subtitle.setTextSize(11.5f);
    subtitle.setFontWeight(400);
    canvas->drawText("Gradient, shadow and stroke in one compact example",
                     kLogicalWidth / 2, 57, subtitle);

    // 3. 用数据描述三个按钮，绘制逻辑只保留一份。
    struct Button {
        float x;
        wsc::Color top;
        wsc::Color bottom;
        const char *label;
        const char *caption;
    };
    const Button buttons[] = {
        {35, wsc::Color(91, 126, 246, 255), wsc::Color(62, 91, 214, 255), "Primary", "Gradient + shadow"},
        {175, wsc::Color(37, 190, 151, 255), wsc::Color(18, 145, 116, 255), "Success", "Soft highlight"},
        {315, wsc::Color(255, 112, 126, 255), wsc::Color(224, 70, 91, 255), "Danger", "High contrast"},
    };

    // 4. 逐个绘制卡片、按钮和说明文字。
    for (const auto &button : buttons) {
        // 白色承载卡片。
        const wsc::RectF card(button.x, 80, 130, 110);
        canvas->drawBoxShadow(card, 12, 0, 11, 0, 4, wsc::Color(35, 49, 83, 24));
        wsc::Paint cardPaint;
        cardPaint.setColor(wsc::Color(255, 255, 255, 245));
        cardPaint.setAntiAlias(true);
        canvas->drawRoundRect(card, 12, cardPaint);

        // 渐变按钮及同色系阴影。
        const wsc::RectF buttonRect(button.x + 15, 105, 100, 38);
        wsc::Paint fill;
        fill.setLinearGradient(button.x, 105, button.x, 143, button.top, button.bottom);
        fill.setAntiAlias(true);
        fill.setShadowLayer(7, 0, 3,
                            wsc::Color(button.bottom.getR(), button.bottom.getG(),
                                       button.bottom.getB(), 78));
        canvas->drawRoundRect(buttonRect, 9, fill);

        // 0.5 个逻辑单位在 DPR=2 时对应一个物理像素。
        wsc::Paint shine;
        shine.setStyle(wsc::Paint::Style::STROKE);
        shine.setStrokeWidth(0.5f);
        shine.setColor(wsc::Color(255, 255, 255, 92));
        shine.setAntiAlias(true);
        canvas->drawRoundRect(buttonRect, 9, shine);

        // 按钮标题和 Paint 特性说明。
        wsc::Paint label;
        label.setColor(wsc::Color(255, 255, 255, 255));
        label.setTextSize(16.0f);
        label.setFontWeight(600);
        label.setTextAlign(wsc::Paint::TextAlign::CENTER);
        label.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
        canvas->drawText(button.label, button.x + 65, 124, label);

        wsc::Paint caption = label;
        caption.setColor(wsc::Color(108, 119, 142, 255));
        caption.setTextSize(11.5f);
        caption.setFontWeight(400);
        canvas->drawText(button.caption, button.x + 65, 166, caption);
    }

    // 5. 先结束当前帧，再保存像素；所有绘制必须发生在 endFrame 之前。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter03_buttons.ppm") ? 0 : 2;
}
