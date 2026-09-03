#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "canvas/Paint.h"
#include "text/CoreTextTextBackend.h"
#include "wsc/Font.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool testLayoutRasterAndCache()
{
    auto backend = wsc::text::createCoreTextTextBackend();
    if (!expect(backend != nullptr, "CoreText backend should be available on Apple platforms")) {
        return false;
    }

    wsc::Paint paint;
    paint.setTextSize(30.0f);
    paint.setFontFamily("Helvetica Neue");
    paint.setTextLocale("zh-CN");
    paint.setLetterSpacing(0.4f);

    const std::string sample = "CoreText 中文 👩🏽‍💻";
    const auto metrics = backend->measureTextMetrics(sample, paint);
    bool ok = expect(metrics.width > 80.0f && metrics.height > 10.0f,
                     "native metrics should cover Latin, CJK, and emoji");
    const auto lines = backend->breakLines(sample + " CoreText", metrics.width * 0.65f, paint);
    ok = expect(lines.size() >= 2, "CoreText typesetter should wrap constrained text") && ok;

    const auto first = backend->renderText(sample, 4.0f, 7.0f, paint);
    const auto second = backend->renderText(sample, 4.0f, 7.0f, paint);
    ok = expect(first.kind == wsc::text::TextRenderKind::Bitmap,
                "CoreText should return a bitmap render result") && ok;
    ok = expect(first.bitmapWidth > 0 && first.bitmapHeight > 0
                    && !first.bitmapPixels.empty(),
                "CoreText bitmap should contain pixels") && ok;
    ok = expect(first.bitmapContentId != 0
                    && first.bitmapContentId == second.bitmapContentId,
                "identical styled text should reuse a stable bitmap identity") && ok;
    ok = expect(backend->hasGlyphForCodepoint(0x4E2D, paint),
                "CoreText fallback should resolve a CJK glyph") && ok;

    constexpr float anchorY = 100.0f;
    wsc::Paint topPaint = paint;
    topPaint.setTextBaseline(wsc::Paint::TextBaseline::TOP);
    wsc::Paint bottomPaint = paint;
    bottomPaint.setTextBaseline(wsc::Paint::TextBaseline::BOTTOM);
    wsc::Paint alphabeticPaint = paint;
    alphabeticPaint.setTextBaseline(wsc::Paint::TextBaseline::ALPHABETIC);
    const auto topAnchor = backend->renderText(sample, 0.0f, anchorY, topPaint);
    const auto bottomAnchor = backend->renderText(sample, 0.0f, anchorY, bottomPaint);
    const auto alphabeticAnchor =
        backend->renderText(sample, 0.0f, anchorY, alphabeticPaint);
    const auto alphabeticBounds =
        backend->measureTextBounds(sample, alphabeticPaint);
    ok = expect(bottomAnchor.drawY < alphabeticAnchor.drawY
                    && alphabeticAnchor.drawY < topAnchor.drawY,
                "CoreText top/alphabetic/bottom anchors should remain distinct") && ok;
    ok = expect(std::abs((alphabeticAnchor.drawY - anchorY)
                         - alphabeticBounds.getY()) < 0.01f,
                "CoreText measurement and rendering should share the alphabetic anchor") && ok;
    return ok;
}

bool testMemoryFontRegistration()
{
    const std::string path = std::string(WHATSCANVAS_SOURCE_DIR)
        + "/third_party/harfbuzz/perf/fonts/Roboto-Regular.ttf";
    std::ifstream input(path, std::ios::binary);
    const std::istreambuf_iterator<char> begin(input);
    const std::istreambuf_iterator<char> end;
    std::vector<std::uint8_t> bytes(begin, end);
    if (!expect(!bytes.empty(), "Roboto memory-font fixture should be readable")) {
        return false;
    }

    auto backend = wsc::text::createCoreTextTextBackend();
    wsc::FontDescriptor descriptor("Roboto", 400, wsc::FontSlant::NORMAL);
    const wsc::FontFace face =
        wsc::FontFace::fromMemory(std::move(descriptor), std::move(bytes));
    bool ok = expect(backend->registerFontFace(face),
                     "CoreText should register an in-memory font descriptor");

    wsc::Paint paint;
    paint.setTextSize(24.0f);
    paint.setFontFamily("Roboto");
    const auto render = backend->renderText("Memory font", 0.0f, 0.0f, paint);
    ok = expect(render.kind == wsc::text::TextRenderKind::Bitmap
                    && render.bitmapContentId != 0,
                "registered memory font should render through CoreText") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = testLayoutRasterAndCache();
    ok = testMemoryFontRegistration() && ok;
    return ok ? 0 : 1;
}
