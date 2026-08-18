#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "text/FontRasterizer.h"
#include "text/TextShaper.h"
#include "wsc/Font.h"

namespace {

std::vector<std::uint32_t> parseCodepoints(const std::string &value)
{
    std::vector<std::uint32_t> result;
    std::istringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token.empty()) return {};
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(token, &consumed, 16);
        if (consumed != token.size() || parsed > 0x10FFFFul) return {};
        result.push_back(static_cast<std::uint32_t>(parsed));
    }
    return result;
}

void appendUtf8(std::string &text, std::uint32_t codepoint)
{
    if (codepoint <= 0x7Fu) {
        text.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FFu) {
        text.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
        text.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else if (codepoint <= 0xFFFFu) {
        text.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
        text.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        text.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    } else {
        text.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
        text.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
        text.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
        text.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "usage: WhatsCanvasFontClusterProbe <font> <hex,codepoints>\n";
        return EXIT_FAILURE;
    }

    std::vector<std::uint32_t> codepoints;
    try {
        codepoints = parseCodepoints(argv[2]);
    } catch (const std::exception &) {
        codepoints.clear();
    }
    if (codepoints.empty()) {
        std::cerr << "invalid codepoint list\n";
        return EXIT_FAILURE;
    }

    std::string text;
    for (std::uint32_t codepoint : codepoints) appendUtf8(text, codepoint);

    const wsc::FontFace face = wsc::FontFace::fromFile(
        wsc::FontDescriptor("cluster-probe"), argv[1]);
    wsc::text::FontRasterizer rasterizer;
    const auto fontData = rasterizer.fontData(face);
    auto shaper = wsc::text::createOpenTypeTextShapingEngine();

    std::cout << "fontData=" << static_cast<bool>(fontData)
              << " shaper=" << static_cast<bool>(shaper) << '\n';
    for (std::uint32_t codepoint : codepoints) {
        const auto glyph = rasterizer.glyphIndex(face, codepoint);
        const auto metrics = rasterizer.glyphMetrics(face, codepoint, 72.0f);
        std::cout << "U+" << std::uppercase << std::hex << codepoint << std::dec
                  << " glyph=" << (glyph ? *glyph : -1)
                  << " metrics=" << static_cast<bool>(metrics) << '\n';
    }
    if (!fontData || !shaper) return 2;

    wsc::text::TextShapeInput input;
    input.normalizedText = text;
    input.pixelSize = 72.0f;
    input.direction = wsc::text::TextDirection::LeftToRight;
    input.fontData = fontData;
    const auto resolver = [&](std::uint32_t codepoint)
        -> std::optional<wsc::text::ResolvedGlyph> {
        const auto metrics = rasterizer.glyphMetrics(face, codepoint, 72.0f);
        if (!metrics) return std::nullopt;
        return wsc::text::ResolvedGlyph{metrics->glyphIndex, metrics->advanceX};
    };
    const auto shaped = shaper->shape(input, resolver);
    std::cout << "shaped=" << static_cast<bool>(shaped);
    bool allRasterized = shaped.has_value();
    if (shaped) {
        std::cout << " glyphs=" << shaped->glyphs.size()
                  << " width=" << shaped->width;
        for (const auto &glyph : shaped->glyphs) {
            const auto rasterized = rasterizer.rasterizeGlyphIndex(
                face, glyph.glyphIndex, glyph.codepoint, 72.0f);
            allRasterized = allRasterized && rasterized.has_value();
            std::cout << "\nshapedGlyph=" << glyph.glyphIndex
                      << " source=U+" << std::uppercase << std::hex
                      << glyph.codepoint << std::dec
                      << " raster=" << static_cast<bool>(rasterized);
        }
    }
    std::cout << '\n';
    return shaped && allRasterized ? EXIT_SUCCESS : 3;
}
