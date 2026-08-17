#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "text/FontRasterizer.h"
#include "wsc/Font.h"

namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 128;
constexpr float kFontSize = 72.0f;
constexpr float kOriginX = 16.0f;
constexpr float kBaselineY = 92.0f;

struct RasterSnapshot
{
    bool found = false;
    int glyph = 0;
    float advance = 0.0f;
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    std::string format;
    std::uint64_t pixelHash = 0;
    int inkPixels = 0;
    int colorPixels = 0;
    int inkLeft = kWidth;
    int inkTop = kHeight;
    int inkRight = -1;
    int inkBottom = -1;
};

std::uint64_t hashBytes(const std::vector<unsigned char> &bytes)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char value : bytes) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ull;
    }
    return hash;
}

RasterSnapshot rasterize(const std::string &family, const std::string &path,
                         std::uint32_t character)
{
    RasterSnapshot snapshot;
    const wsc::FontFace face = wsc::FontFace::fromFile(
        wsc::FontDescriptor(family), path);
    wsc::text::FontRasterizer rasterizer;
    const auto rasterized = rasterizer.rasterizeGlyph(face, character, kFontSize);
    if (!rasterized) return snapshot;

    const wsc::text::GlyphBitmap &bitmap = rasterized->bitmap;
    snapshot.found = true;
    snapshot.glyph = rasterized->key.glyphIndex;
    snapshot.advance = bitmap.advanceX;
    snapshot.left = bitmap.bearingX;
    snapshot.top = bitmap.bearingY;
    snapshot.right = bitmap.bearingX + static_cast<float>(bitmap.width);
    snapshot.bottom = bitmap.bearingY + static_cast<float>(bitmap.height);
    snapshot.format = bitmap.format == wsc::text::GlyphBitmapFormat::RGBA
        ? "rgba" : "alpha";

    std::vector<unsigned char> scene(
        static_cast<std::size_t>(kWidth) * kHeight * 4u, 0);
    const int destinationX = static_cast<int>(std::lround(kOriginX + bitmap.bearingX));
    const int destinationY = static_cast<int>(std::lround(kBaselineY + bitmap.bearingY));
    for (int row = 0; row < bitmap.height; ++row) {
        const int y = destinationY + row;
        if (y < 0 || y >= kHeight) continue;
        for (int column = 0; column < bitmap.width; ++column) {
            const int x = destinationX + column;
            if (x < 0 || x >= kWidth) continue;
            const std::size_t source = static_cast<std::size_t>(row)
                    * static_cast<std::size_t>(bitmap.width)
                + static_cast<std::size_t>(column);
            const std::size_t destination =
                (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4u;
            unsigned char red = 0;
            unsigned char green = 0;
            unsigned char blue = 0;
            unsigned char alpha = 0;
            if (bitmap.format == wsc::text::GlyphBitmapFormat::RGBA
                && source * 4u + 3u < bitmap.rgbaPixels.size()) {
                red = bitmap.rgbaPixels[source * 4u + 0u];
                green = bitmap.rgbaPixels[source * 4u + 1u];
                blue = bitmap.rgbaPixels[source * 4u + 2u];
                alpha = bitmap.rgbaPixels[source * 4u + 3u];
            } else if (source < bitmap.alphaPixels.size()) {
                alpha = bitmap.alphaPixels[source];
            }
            if (alpha == 0) {
                red = green = blue = 0;
            }
            scene[destination + 0u] = red;
            scene[destination + 1u] = green;
            scene[destination + 2u] = blue;
            scene[destination + 3u] = alpha;
            if (alpha == 0) continue;
            ++snapshot.inkPixels;
            snapshot.inkLeft = std::min(snapshot.inkLeft, x);
            snapshot.inkTop = std::min(snapshot.inkTop, y);
            snapshot.inkRight = std::max(snapshot.inkRight, x);
            snapshot.inkBottom = std::max(snapshot.inkBottom, y);
            if (red != green || green != blue) ++snapshot.colorPixels;
        }
    }
    snapshot.pixelHash = hashBytes(scene);
    return snapshot;
}

void appendSnapshot(std::ostringstream &output, const RasterSnapshot &snapshot)
{
    output << std::setprecision(9)
           << "{\"found\":" << (snapshot.found ? "true" : "false");
    if (snapshot.found) {
        output << ",\"glyph\":" << snapshot.glyph
               << ",\"advance\":" << snapshot.advance
               << ",\"glyphBounds\":[" << snapshot.left << ',' << snapshot.top
               << ',' << snapshot.right << ',' << snapshot.bottom << ']'
               << ",\"format\":\"" << snapshot.format << "\""
               << ",\"pixelHash\":\"" << snapshot.pixelHash << "\""
               << ",\"inkPixels\":" << snapshot.inkPixels
               << ",\"colorPixels\":" << snapshot.colorPixels
               << ",\"inkBounds\":[" << snapshot.inkLeft << ',' << snapshot.inkTop
               << ',' << snapshot.inkRight << ',' << snapshot.inkBottom << ']';
    }
    output << '}';
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 5) {
        std::cerr << "Usage: WhatsCanvasFontRasterProbe <latin-font> <cjk-font> "
                     "<colr-emoji-font> <bitmap-emoji-font>\n";
        return EXIT_FAILURE;
    }
    const RasterSnapshot latin = rasterize("oracle-latin", argv[1], 0x0041);
    const RasterSnapshot cjk = rasterize("oracle-cjk", argv[2], 0x4c2e);
    const RasterSnapshot colrEmoji = rasterize("oracle-colr-emoji", argv[3], 0x3297);
    const RasterSnapshot bitmapEmoji = rasterize("oracle-bitmap-emoji", argv[4], 0x2049);
    if (!latin.found || !cjk.found || !colrEmoji.found) {
        std::cerr << "Rasterization failed: latin=" << latin.found
                  << " cjk=" << cjk.found
                  << " colrEmoji=" << colrEmoji.found
                  << " bitmapEmoji=" << bitmapEmoji.found << '\n';
        return EXIT_FAILURE;
    }

    std::ostringstream output;
    output << "{\"schema\":\"whatscanvas.font-raster.v1\","
              "\"engine\":\"whatscanvas-freetype\",\"width\":"
           << kWidth << ",\"height\":" << kHeight << ",\"fontSize\":"
           << kFontSize << ",\"latin\":";
    appendSnapshot(output, latin);
    output << ",\"simplifiedCjk\":";
    appendSnapshot(output, cjk);
    output << ",\"colrEmoji\":";
    appendSnapshot(output, colrEmoji);
    output << ",\"bitmapEmoji\":";
    appendSnapshot(output, bitmapEmoji);
    output << "}\n";
    std::cout << output.str();
    return EXIT_SUCCESS;
}
