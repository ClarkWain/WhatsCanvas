#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_android.h"
#include "include/ports/SkFontScanner_FreeType.h"
#include "src/core/SkColorPriv.h"

namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 128;
constexpr float kFontSize = 72.0f;

struct RasterSnapshot
{
    bool found = false;
    std::uint16_t glyph = 0;
    float advance = 0.0f;
    SkRect glyphBounds = SkRect::MakeEmpty();
    std::uint64_t pixelHash = 0;
    int inkPixels = 0;
    int colorPixels = 0;
    int left = kWidth;
    int top = kHeight;
    int right = -1;
    int bottom = -1;
};

std::uint64_t hashPixels(const std::vector<SkPMColor> &pixels)
{
    std::uint64_t hash = 14695981039346656037ull;
    const auto *bytes = reinterpret_cast<const unsigned char *>(pixels.data());
    for (std::size_t index = 0; index < pixels.size() * sizeof(SkPMColor); ++index) {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= 1099511628211ull;
    }
    return hash;
}

RasterSnapshot rasterize(const sk_sp<SkTypeface> &typeface, SkUnichar character)
{
    RasterSnapshot snapshot;
    if (!typeface) return snapshot;
    snapshot.found = true;
    snapshot.glyph = typeface->unicharToGlyph(character);
    if (snapshot.glyph == 0) return snapshot;

    SkFont font(typeface, kFontSize);
    font.setEdging(SkFont::Edging::kAntiAlias);
    font.setHinting(SkFontHinting::kNone);
    font.setSubpixel(false);
    snapshot.advance = font.measureText(&snapshot.glyph, sizeof(snapshot.glyph),
                                        SkTextEncoding::kGlyphID,
                                        &snapshot.glyphBounds);

    std::vector<SkPMColor> pixels(static_cast<std::size_t>(kWidth) * kHeight, 0);
    std::unique_ptr<SkCanvas> canvas = SkCanvas::MakeRasterDirectN32(
        kWidth, kHeight, pixels.data(), static_cast<std::size_t>(kWidth) * sizeof(SkPMColor));
    if (!canvas) {
        snapshot.found = false;
        return snapshot;
    }
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorBLACK);
    const SkPoint position = SkPoint::Make(16.0f, 92.0f);
    canvas->drawGlyphs({&snapshot.glyph, 1}, {&position, 1}, SkPoint::Make(0, 0),
                       font, paint);
    snapshot.pixelHash = hashPixels(pixels);

    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const SkPMColor pixel = pixels[static_cast<std::size_t>(y) * kWidth + x];
            if (SkGetPackedA32(pixel) == 0) continue;
            ++snapshot.inkPixels;
            snapshot.left = std::min(snapshot.left, x);
            snapshot.top = std::min(snapshot.top, y);
            snapshot.right = std::max(snapshot.right, x);
            snapshot.bottom = std::max(snapshot.bottom, y);
            const unsigned red = SkGetPackedR32(pixel);
            const unsigned green = SkGetPackedG32(pixel);
            const unsigned blue = SkGetPackedB32(pixel);
            if (red != green || green != blue) ++snapshot.colorPixels;
        }
    }
    return snapshot;
}

void appendSnapshot(std::ostringstream &output, const RasterSnapshot &snapshot)
{
    output << std::setprecision(9)
           << "{\"found\":" << (snapshot.found ? "true" : "false");
    if (snapshot.found) {
        output << ",\"glyph\":" << snapshot.glyph
               << ",\"advance\":" << snapshot.advance
               << ",\"glyphBounds\":[" << snapshot.glyphBounds.left() << ','
               << snapshot.glyphBounds.top() << ',' << snapshot.glyphBounds.right() << ','
               << snapshot.glyphBounds.bottom() << ']'
               << ",\"pixelHash\":\"" << snapshot.pixelHash << "\""
               << ",\"inkPixels\":" << snapshot.inkPixels
               << ",\"colorPixels\":" << snapshot.colorPixels
               << ",\"inkBounds\":[" << snapshot.left << ',' << snapshot.top << ','
               << snapshot.right << ',' << snapshot.bottom << ']';
    }
    output << '}';
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "Usage: SkiaFontRasterProbe <fonts.xml> <font-dir>\n";
        return EXIT_FAILURE;
    }
    std::string basePath = argv[2];
    if (!basePath.empty() && basePath.back() != '/' && basePath.back() != '\\') {
        basePath.push_back('/');
    }
    const SkFontMgr_Android_CustomFonts custom{
        SkFontMgr_Android_CustomFonts::kOnlyCustom,
        basePath.c_str(),
        argv[1],
        nullptr,
        true,
    };
    sk_sp<SkFontMgr> manager = SkFontMgr_New_Android(
        &custom, SkFontScanner_Make_FreeType());
    if (!manager) return EXIT_FAILURE;

    const SkFontStyle style(400, SkFontStyle::kNormal_Width,
                            SkFontStyle::kUpright_Slant);
    const char *english[] = {"en"};
    const char *simplifiedChinese[] = {"zh-Hans"};
    const char *emojiPresentation[] = {"und-Zsye"};
    const sk_sp<SkTypeface> latin = manager->matchFamilyStyleCharacter(
        "oracle-variable", style, english, 1, 0x0041);
    const sk_sp<SkTypeface> cjk = manager->matchFamilyStyleCharacter(
        "oracle-variable", style, simplifiedChinese, 1, 0x4c2e);
    const sk_sp<SkTypeface> colrEmoji = manager->matchFamilyStyleCharacter(
        "oracle-variable", style, emojiPresentation, 1, 0x3297);
    const sk_sp<SkTypeface> bitmapEmoji = manager->matchFamilyStyleCharacter(
        "oracle-variable", style, emojiPresentation, 1, 0x2049);

    std::ostringstream output;
    output << "{\"schema\":\"whatscanvas.skia-font-raster.v1\","
              "\"engine\":\"skia-android-freetype\",\"width\":"
           << kWidth << ",\"height\":" << kHeight << ",\"fontSize\":"
           << kFontSize << ",\"latin\":";
    appendSnapshot(output, rasterize(latin, 0x0041));
    output << ",\"simplifiedCjk\":";
    appendSnapshot(output, rasterize(cjk, 0x4c2e));
    output << ",\"colrEmoji\":";
    appendSnapshot(output, rasterize(colrEmoji, 0x3297));
    output << ",\"bitmapEmoji\":";
    appendSnapshot(output, rasterize(bitmapEmoji, 0x2049));
    output << "}\n";
    std::cout << output.str();
    return EXIT_SUCCESS;
}
