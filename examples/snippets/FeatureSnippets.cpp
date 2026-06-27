#include "wsc/wsc.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

void fontFallbackSnippet()
{
    wsc::FontManager fonts;
    fonts.registerFontFile(wsc::FontDescriptor("Inter", 400), "assets/fonts/Inter-Regular.ttf");
    fonts.registerFontFile(wsc::FontDescriptor("NotoSansCJK", 400), "assets/fonts/NotoSansCJK-Regular.otf");
    fonts.registerFontMemory(wsc::FontDescriptor("EmojiFallback", 400),
                             std::vector<std::uint8_t>{0x00, 0x01, 0x02, 0x03});

    fonts.addFallbackFamily("Inter", "NotoSansCJK");
    fonts.addFallbackFamily("Inter", "EmojiFallback");

    const std::vector<std::string> families = fonts.resolveFamilies("Inter");
    std::cout << "FONT_FALLBACK_FAMILY_COUNT=" << families.size() << std::endl;
}

void multilineTextSnippet(wsc::Canvas &canvas)
{
    wsc::Paint textPaint;
    textPaint.setTextSize(18.0f);
    textPaint.setTextAlign(wsc::Paint::TextAlign::LEFT);
    textPaint.setTextBaseline(wsc::Paint::TextBaseline::TOP);
    textPaint.setLetterSpacing(0.5f);
    textPaint.setColor(wsc::Color(32, 38, 54, 255));

    const wsc::RectF textBox(24.0f, 24.0f, 280.0f, 120.0f);
    const std::string text = "Multiline text uses layoutTextBox for measurement and drawTextBox for rendering.";
    const auto lines = canvas.layoutTextBox(text, textBox, 22.0f, 4, true, textPaint);
    std::cout << "MULTILINE_TEXT_LINE_COUNT=" << lines.size() << std::endl;
    canvas.drawTextBox(text, textBox, 22.0f, 4, true, textPaint);
}

void imagePatternSnippet(wsc::Canvas &canvas, wsc::Image &image)
{
    wsc::Paint patternPaint;
    patternPaint.setAlpha(0.85f);
    patternPaint.setImageSampling(wsc::Paint::ImageSampling::NEAREST);
    patternPaint.setImageTileMode(wsc::Paint::ImageTileMode::MIRROR);

    canvas.save();
    canvas.translate(24.0f, 180.0f);
    canvas.rotate(0.08f);
    canvas.drawImageTiled(image, wsc::RectF(0.0f, 0.0f, 240.0f, 96.0f), 32.0f, 32.0f, patternPaint);
    canvas.restore();
}

bool externalTextureSnippet(wsc::Canvas &canvas, std::uint32_t nativeTextureId)
{
    wsc::Image externalTexture;
    if (!externalTexture.wrapExternalTexture(canvas, nativeTextureId, 256, 256, false)) {
        return false;
    }

    wsc::Paint paint;
    paint.setImageSampling(wsc::Paint::ImageSampling::LINEAR);
    paint.setImageTileMode(wsc::Paint::ImageTileMode::CLAMP);
    canvas.drawImage(externalTexture, wsc::RectF(320.0f, 24.0f, 128.0f, 128.0f), paint);
    return true;
}

} // namespace

int main()
{
    wsc::Canvas canvas;
    canvas.setSize(480, 320);

    fontFallbackSnippet();
    multilineTextSnippet(canvas);

    wsc::Image placeholderImage;
    imagePatternSnippet(canvas, placeholderImage);

    const bool wrapped = externalTextureSnippet(canvas, 0);
    std::cout << "EXTERNAL_TEXTURE_WRAPPED=" << (wrapped ? 1 : 0) << std::endl;
    canvas.releaseResources();
    return 0;
}
