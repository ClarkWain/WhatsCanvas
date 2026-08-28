// Chapter 07 comprehensive example: multilingual chat bubbles and wrapping.
#include <wsc/FontSystem.h>
#include <wsc/wsc.h>

int main()
{
    // 1. 输出 720x820 物理像素，但使用 360x410 的逻辑坐标布局。
    // DPR 只决定逻辑单位如何映射到物理像素，不需要手动放大每个坐标。
    constexpr int kPhysicalWidth = 720;
    constexpr int kPhysicalHeight = 820;
    constexpr float kDpr = 2.0f;
    constexpr float kLogicalWidth = kPhysicalWidth / kDpr;
    constexpr float kLogicalHeight = kPhysicalHeight / kDpr;

    auto canvas = wsc::Canvas::create(
        wsc::Canvas::Backend::Software, kPhysicalWidth, kPhysicalHeight);
    if (!canvas) return 1;
    canvas->setDevicePixelRatio(kDpr);
    if (!canvas->initializeContext()) return 1;
    for (const auto &face : wsc::FontSystem::defaultSystemFontFaces()) {
        canvas->registerFontFace(face);
    }
    canvas->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());

    // 2. 绘制聊天页面背景和标题栏。
    canvas->beginFrame();
    wsc::Paint background;
    background.setColor(wsc::Color(239, 243, 249, 255));
    canvas->drawRect(wsc::RectF(0, 0, kLogicalWidth, kLogicalHeight), background);

    wsc::Paint header;
    header.setLinearGradient(0, 0, kLogicalWidth, 58,
        wsc::Color(82, 116, 242, 255), wsc::Color(93, 86, 216, 255));
    canvas->drawRect(wsc::RectF(0, 0, kLogicalWidth, 58), header);

    wsc::Paint title;
    title.setColor(wsc::Color(255, 255, 255, 255));
    title.setTextSize(20.0f);
    title.setFontWeight(650);
    title.setTextAlign(wsc::Paint::TextAlign::CENTER);
    title.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    canvas->drawText("WhatsCanvas Chat", kLogicalWidth / 2, 22, title);
    title.setTextSize(10.5f);
    title.setFontWeight(400);
    title.setColor(wsc::Color(235, 239, 255, 210));
    canvas->drawText("CJK · RTL · Emoji · Auto wrapping", kLogicalWidth / 2, 42, title);

    // 3. 气泡函数统一处理左右布局、阴影、RTL 和自动换行。
    auto drawBubble = [&](const char *text, float y, float width, bool isMe,
                          bool rtl = false, bool multiline = false) {
        const float height = multiline ? 58.0f : 44.0f;
        const float x = isMe ? kLogicalWidth - 20.0f - width : 20.0f;
        const wsc::RectF bounds(x, y, width, height);
        canvas->drawBoxShadow(bounds, 18, 0, 5, 0, 2, wsc::Color(33, 48, 78, 28));
        wsc::Paint bubble;
        bubble.setColor(isMe ? wsc::Color(82, 116, 242, 255) : wsc::Color(255, 255, 255, 255));
        bubble.setAntiAlias(true);
        canvas->drawRoundRect(bounds, 18, bubble);

        wsc::Paint textPaint;
        textPaint.setColor(isMe ? wsc::Color(255, 255, 255, 255) : wsc::Color(35, 43, 60, 255));
        // 16 个逻辑单位在 DPR=2 时栅格化为约 32 个物理像素。
        // 这里显式使用正文的 Regular 字重，不靠加粗弥补字号过小。
        textPaint.setTextSize(16.0f);
        textPaint.setFontWeight(400);
        textPaint.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
        textPaint.setTextLocale(rtl ? "ar" : "zh-CN");
        if (rtl) {
            textPaint.setTextAlign(wsc::Paint::TextAlign::RIGHT);
            canvas->drawText(text, x + width - 16, y + height / 2, textPaint);
        } else if (multiline) {
            textPaint.setTextBaseline(wsc::Paint::TextBaseline::TOP);
            canvas->drawTextBox(text, wsc::RectF(x + 16, y + 8, width - 32, height - 12),
                                22.0f, 2, true, textPaint);
        } else {
            canvas->drawText(text, x + 16, y + height / 2, textPaint);
        }
    };

    // 4. 使用中文、阿拉伯文和 Emoji 验证字体 fallback 与 shaping。
    drawBubble(u8"你好，欢迎体验 WhatsCanvas。", 72, 235, false);
    drawBubble(u8"文字排版看起来很顺滑。", 122, 215, true);
    drawBubble(u8"مرحبا! يدعم النص من اليمين.", 172, 240, false, true);
    drawBubble(u8"Emoji fallback works ✨ 🎨", 222, 220, true);
    drawBubble(u8"长文本会自动换行；超过指定行数时，可以使用省略号收尾。",
               272, 265, false, false, true);

    // 5. 绘制底部输入框和发送按钮。
    const wsc::RectF input(20, 346, 320, 48);
    canvas->drawBoxShadow(input, 24, 0, 6, 0, 2, wsc::Color(33, 48, 78, 24));
    wsc::Paint inputPaint;
    inputPaint.setColor(wsc::Color(255, 255, 255, 255));
    inputPaint.setAntiAlias(true);
    canvas->drawRoundRect(input, 24, inputPaint);
    wsc::Paint placeholder;
    placeholder.setColor(wsc::Color(145, 155, 177, 255));
    placeholder.setTextSize(15.0f);
    placeholder.setFontWeight(400);
    placeholder.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    canvas->drawText(u8"输入消息…", 34, 370, placeholder);
    inputPaint.setColor(wsc::Color(82, 116, 242, 255));
    canvas->drawCircle(320, 370, 16, inputPaint);

    // 6. 提交并保存多语言聊天效果图。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter07_chat.ppm") ? 0 : 2;
}
