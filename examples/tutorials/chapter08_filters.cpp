// Chapter 08 comprehensive example: frosted glass and inner shadow.
#include <wsc/FontSystem.h>
#include <wsc/wsc.h>

int main()
{
    // 1. 创建画布并注册界面文字使用的系统字体。
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 960, 600);
    if (!canvas || !canvas->initializeContext()) return 1;
    for (const auto &face : wsc::FontSystem::defaultSystemFontFaces()) {
        canvas->registerFontFace(face);
    }
    canvas->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());

    // 2. 先绘制渐变背景和三个彩色圆；它们是毛玻璃的输入背景。
    canvas->beginFrame();
    wsc::Paint background;
    background.setLinearGradient(0, 0, 960, 600,
        wsc::Color(37, 30, 82, 255), wsc::Color(20, 91, 118, 255));
    canvas->drawRect(wsc::RectF(0, 0, 960, 600), background);

    wsc::Paint orb;
    orb.setAntiAlias(true);
    orb.setColor(wsc::Color(255, 111, 145, 220));
    orb.setShadowLayer(28, 0, 12, wsc::Color(255, 95, 142, 95));
    canvas->drawCircle(250, 190, 112, orb);
    orb.setColor(wsc::Color(66, 215, 202, 220));
    orb.setShadowLayer(30, 0, 12, wsc::Color(39, 210, 196, 90));
    canvas->drawCircle(760, 390, 150, orb);
    orb.setColor(wsc::Color(254, 199, 90, 215));
    canvas->drawCircle(610, 110, 72, orb);

    // 3. 绘制页面标题。
    wsc::Paint title;
    title.setColor(wsc::Color(255, 255, 255, 245));
    title.setTextSize(34.0f);
    title.setFontWeight(650);
    title.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    canvas->drawText("Layer filters", 58, 62, title);
    title.setTextSize(16.0f);
    title.setFontWeight(400);
    title.setColor(wsc::Color(225, 232, 249, 190));
    canvas->drawText("Backdrop blur and inset depth, rendered by the Software backend", 58, 100, title);

    // 4. backdropFilter 处理面板后方已经绘制的像素。
    const wsc::RectF glassBounds(70, 150, 500, 300);
    wsc::Paint composite;
    composite.setColor(wsc::Color(255, 255, 255, 255));
    wsc::LayerOptions glass;
    glass.setBackdropFilter(wsc::ImageFilter::frostedGlass(8.0f, 1.16f, 1.04f, 1.02f, 0.004f));

    // saveLayer 的边界是矩形；先使用同尺寸的圆角路径裁剪，避免右上角等位置
    // 泄漏矩形的 backdropFilter 结果。
    canvas->save();
    wsc::Path glassClip;
    glassClip.addRoundRect(glassBounds, 30.0f);
    canvas->clipPath(glassClip);
    canvas->saveLayer(glassBounds, composite, glass);
    wsc::Paint tint;
    tint.setColor(wsc::Color(239, 246, 255, 54));
    tint.setAntiAlias(true);
    canvas->drawRoundRect(glassBounds, 30, tint);
    canvas->restore(); // 合成毛玻璃图层。
    canvas->restore(); // 解除圆角裁剪。

    wsc::Paint border;
    border.setStyle(wsc::Paint::Style::STROKE);
    border.setStrokeWidth(1.5f);
    border.setColor(wsc::Color(255, 255, 255, 105));
    border.setAntiAlias(true);
    canvas->drawRoundRect(glassBounds, 30, border);

    // 5. 面板内容在 restore 之后绘制，因此文字本身不会被模糊。
    wsc::Paint glassTitle = title;
    glassTitle.setColor(wsc::Color(255, 255, 255, 250));
    glassTitle.setTextSize(27.0f);
    glassTitle.setFontWeight(650);
    canvas->drawText("Frosted glass", 112, 215, glassTitle);
    glassTitle.setTextSize(17.0f);
    glassTitle.setFontWeight(400);
    glassTitle.setColor(wsc::Color(239, 244, 255, 205));
    glassTitle.setTextBaseline(wsc::Paint::TextBaseline::TOP);
    canvas->drawTextBox("The backdrop stays visible, while blur and color adjustment separate the panel from the scene.",
                        wsc::RectF(112, 250, 410, 100), 26.0f, 3, true, glassTitle);

    // 6. imageFilter 只处理当前图层内容，用于实现输入框内阴影。
    const wsc::RectF fieldBounds(112, 362, 416, 58);
    wsc::LayerOptions inset;
    inset.setImageFilter(wsc::ImageFilter::innerShadow(8.0f, 0, 4, wsc::Color(13, 23, 50, 125)));
    canvas->saveLayer(fieldBounds, composite, inset);
    wsc::Paint field;
    field.setColor(wsc::Color(255, 255, 255, 42));
    field.setAntiAlias(true);
    canvas->drawRoundRect(fieldBounds, 16, field);
    canvas->restore();
    glassTitle.setTextSize(16.0f);
    glassTitle.setColor(wsc::Color(255, 255, 255, 175));
    glassTitle.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    canvas->drawText("Inner shadow", 136, 392, glassTitle);

    // 7. 提交并保存滤镜效果图。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter08_filters.ppm") ? 0 : 2;
}
