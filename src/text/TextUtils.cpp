#include "text/TextUtils.h"

#include <algorithm>
#include <cmath>

#include "stb_easy_font.h"

namespace {

constexpr float kPointEpsilon = 0.0001f;

size_t estimateAsciiTextVertexBufferBytes(const std::string &asciiText)
{
    constexpr size_t kBytesPerQuad = 64;
    size_t quadCount = 0;

    for (char character : asciiText) {
        if (character == '\n') {
            continue;
        }

        const unsigned char glyph = static_cast<unsigned char>(character);
        if (glyph < 32 || glyph > 126) {
            continue;
        }

        const size_t glyphIndex = static_cast<size_t>(glyph - 32);
        const int horizontalSegments = stb_easy_font_charinfo[glyphIndex + 1].h_seg
            - stb_easy_font_charinfo[glyphIndex].h_seg;
        const int verticalSegments = stb_easy_font_charinfo[glyphIndex + 1].v_seg
            - stb_easy_font_charinfo[glyphIndex].v_seg;
        quadCount += static_cast<size_t>(std::max(0, horizontalSegments) + std::max(0, verticalSegments));
    }

    return std::max(kBytesPerQuad, quadCount * kBytesPerQuad);
}

} // namespace

namespace prismcanvas::text {

std::string sanitizeTextToAscii(const std::string &text)
{
    std::string sanitized;
    sanitized.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch < 0x80) {
            if (ch == '\n') {
                sanitized.push_back('\n');
            } else if (ch == '\t') {
                sanitized.append("    ");
            } else if (ch >= 32 && ch <= 126) {
                sanitized.push_back(static_cast<char>(ch));
            } else {
                sanitized.push_back(' ');
            }
            ++i;
            continue;
        }

        size_t advance = 1;
        if ((ch & 0xE0) == 0xC0) {
            advance = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            advance = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            advance = 4;
        }

        if (i + advance > text.size()) {
            advance = 1;
        }

        sanitized.push_back('?');
        i += advance;
    }

    return sanitized;
}

float measureAsciiTextWidth(const std::string &asciiText, float scale, float letterSpacing)
{
    if (asciiText.empty()) {
        return 0.0f;
    }

    const float baseWidth = static_cast<float>(stb_easy_font_width(const_cast<char *>(asciiText.c_str()))) * scale;
    const float spacing = std::isfinite(letterSpacing) ? letterSpacing : 0.0f;
    return std::max(0.0f, baseWidth + spacing * static_cast<float>(asciiText.size() - 1));
}

float measureAsciiTextHeight(const std::string &asciiText, float scale)
{
    if (asciiText.empty()) {
        return 0.0f;
    }

    return static_cast<float>(stb_easy_font_height(const_cast<char *>(asciiText.c_str()))) * scale;
}

float textBaselineOffset(Paint::TextBaseline baseline, float textHeight)
{
    switch (baseline) {
    case Paint::TextBaseline::TOP:
        return 0.0f;
    case Paint::TextBaseline::MIDDLE:
        return -textHeight * 0.5f;
    case Paint::TextBaseline::BOTTOM:
        return -textHeight;
    }

    return 0.0f;
}

std::vector<float> buildTextVertices(const std::string &asciiText, float x, float y, float scale, float letterSpacing)
{
    if (asciiText.empty()) {
        return {};
    }

    if (std::abs(letterSpacing) > kPointEpsilon) {
        std::vector<float> vertices;
        float cursorX = x;
        for (char character : asciiText) {
            const std::string glyph(1, character);
            if (character == '\n') {
                cursorX = x;
                y += 12.0f * scale;
                continue;
            }

            auto glyphVertices = buildTextVertices(glyph, cursorX, y, scale);
            vertices.insert(vertices.end(), glyphVertices.begin(), glyphVertices.end());
            cursorX += measureAsciiTextWidth(glyph, scale, 0.0f) + letterSpacing;
        }
        return vertices;
    }

    const size_t bufferSize = estimateAsciiTextVertexBufferBytes(asciiText);
    std::vector<char> buffer(bufferSize, 0);
    int quads = stb_easy_font_print(x, y, const_cast<char *>(asciiText.c_str()), nullptr, buffer.data(), static_cast<int>(buffer.size()));
    if (quads <= 0) {
        return {};
    }

    const auto *quadData = reinterpret_cast<const float *>(buffer.data());
    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>(quads) * 12);

    for (int quad = 0; quad < quads; ++quad) {
        const float x0 = quadData[quad * 16 + 0] * scale;
        const float y0 = quadData[quad * 16 + 1] * scale;
        const float x1 = quadData[quad * 16 + 4] * scale;
        const float y1 = quadData[quad * 16 + 5] * scale;
        const float x2 = quadData[quad * 16 + 8] * scale;
        const float y2 = quadData[quad * 16 + 9] * scale;
        const float x3 = quadData[quad * 16 + 12] * scale;
        const float y3 = quadData[quad * 16 + 13] * scale;

        vertices.insert(vertices.end(), {
            x0, y0,
            x1, y1,
            x2, y2,
            x0, y0,
            x2, y2,
            x3, y3
        });
    }

    return vertices;
}

} // namespace prismcanvas::text