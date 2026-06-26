#include "text/TextUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "stb_easy_font.h"

namespace {

constexpr float kPointEpsilon = 0.0001f;
constexpr std::uint32_t kReplacementCodepoint = 0xFFFD;
const char *kReplacementUtf8 = "\xEF\xBF\xBD";

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

namespace wsc::text {

std::vector<Utf8Codepoint> decodeUtf8(const std::string &text)
{
    std::vector<Utf8Codepoint> codepoints;
    codepoints.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        Utf8Codepoint codepoint;
        codepoint.offset = i;

        if (ch < 0x80) {
            codepoint.value = ch;
            codepoint.length = 1;
            codepoint.valid = true;
            codepoints.push_back(codepoint);
            ++i;
            continue;
        }

        std::size_t advance = 0;
        std::uint32_t value = 0;
        std::uint32_t minimumValue = 0;
        if ((ch & 0xE0) == 0xC0) {
            advance = 2;
            value = ch & 0x1F;
            minimumValue = 0x80;
        } else if ((ch & 0xF0) == 0xE0) {
            advance = 3;
            value = ch & 0x0F;
            minimumValue = 0x800;
        } else if ((ch & 0xF8) == 0xF0) {
            advance = 4;
            value = ch & 0x07;
            minimumValue = 0x10000;
        }

        bool valid = advance > 0 && i + advance <= text.size();
        if (valid) {
            for (std::size_t j = 1; j < advance; ++j) {
                const unsigned char continuation = static_cast<unsigned char>(text[i + j]);
                if ((continuation & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
                value = (value << 6) | (continuation & 0x3F);
            }
        }

        if (valid) {
            valid = value >= minimumValue
                && value <= 0x10FFFF
                && !(value >= 0xD800 && value <= 0xDFFF);
        }

        if (!valid) {
            codepoint.value = kReplacementCodepoint;
            codepoint.length = 1;
            codepoint.valid = false;
            codepoints.push_back(codepoint);
            ++i;
            continue;
        }

        codepoint.value = value;
        codepoint.length = advance;
        codepoint.valid = true;
        codepoints.push_back(codepoint);
        i += advance;
    }

    return codepoints;
}

bool isValidUtf8(const std::string &text)
{
    const auto codepoints = decodeUtf8(text);
    return std::all_of(codepoints.begin(), codepoints.end(), [](const Utf8Codepoint &codepoint) {
        return codepoint.valid;
    });
}

std::string normalizeUtf8ForText(const std::string &text)
{
    std::string normalized;
    normalized.reserve(text.size());

    const auto codepoints = decodeUtf8(text);
    for (const Utf8Codepoint &codepoint : codepoints) {
        if (!codepoint.valid) {
            normalized.append(kReplacementUtf8);
            continue;
        }

        if (codepoint.value == '\n') {
            normalized.push_back('\n');
        } else if (codepoint.value == '\t') {
            normalized.append("    ");
        } else if (codepoint.value < 32) {
            normalized.push_back(' ');
        } else {
            normalized.append(text, codepoint.offset, codepoint.length);
        }
    }

    return normalized;
}

std::string makeAsciiFallbackText(const std::string &text, char replacement)
{
    std::string fallback;
    fallback.reserve(text.size());

    const auto codepoints = decodeUtf8(text);
    for (const Utf8Codepoint &codepoint : codepoints) {
        if (!codepoint.valid) {
            fallback.push_back(replacement);
            continue;
        }

        if (codepoint.value == '\n') {
            fallback.push_back('\n');
        } else if (codepoint.value == '\t') {
            fallback.append("    ");
        } else if (codepoint.value >= 32 && codepoint.value <= 126) {
            fallback.push_back(static_cast<char>(codepoint.value));
        } else if (codepoint.value < 32) {
            fallback.push_back(' ');
        } else {
            fallback.push_back(replacement);
        }
    }

    return fallback;
}

std::size_t countUtf8Codepoints(const std::string &text)
{
    return decodeUtf8(text).size();
}

std::string sanitizeTextToAscii(const std::string &text)
{
    return makeAsciiFallbackText(text);
}

float measureAsciiTextWidth(const std::string &asciiText, float scale, float letterSpacing)
{
    if (asciiText.empty()) {
        return 0.0f;
    }

    const float baseWidth = static_cast<float>(stb_easy_font_width(const_cast<char *>(asciiText.c_str()))) * scale;
    const float spacing = std::isfinite(letterSpacing) ? letterSpacing : 0.0f;
    return std::max(0.0f, baseWidth + spacing * static_cast<float>(countUtf8Codepoints(asciiText) - 1));
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

} // namespace wsc::text
