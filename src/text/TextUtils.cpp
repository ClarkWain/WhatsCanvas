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

bool isBreakWhitespace(std::uint32_t codepoint)
{
    return codepoint == ' '
        || codepoint == '\t'
        || codepoint == 0x1680
        || ((codepoint >= 0x2000 && codepoint <= 0x200A) && codepoint != 0x2007)
        || codepoint == 0x2028
        || codepoint == 0x2029
        || codepoint == 0x205F
        || codepoint == 0x3000;
}

bool isLineBreak(std::uint32_t codepoint)
{
    return codepoint == '\n' || codepoint == '\r';
}

bool isCombiningMark(std::uint32_t codepoint)
{
    return (codepoint >= 0x0300 && codepoint <= 0x036F)
        || (codepoint >= 0x0483 && codepoint <= 0x0489)
        || (codepoint >= 0x0591 && codepoint <= 0x05BD)
        || codepoint == 0x05BF
        || (codepoint >= 0x05C1 && codepoint <= 0x05C2)
        || (codepoint >= 0x05C4 && codepoint <= 0x05C5)
        || codepoint == 0x05C7
        || (codepoint >= 0x0610 && codepoint <= 0x061A)
        || (codepoint >= 0x064B && codepoint <= 0x065F)
        || codepoint == 0x0670
        || (codepoint >= 0x06D6 && codepoint <= 0x06ED)
        || codepoint == 0x0711
        || (codepoint >= 0x0730 && codepoint <= 0x074A)
        || (codepoint >= 0x07A6 && codepoint <= 0x07B0)
        || (codepoint >= 0x07EB && codepoint <= 0x07F3)
        || (codepoint >= 0x0816 && codepoint <= 0x082D)
        || (codepoint >= 0x0859 && codepoint <= 0x085B)
        || (codepoint >= 0x08D3 && codepoint <= 0x0903)
        || (codepoint >= 0x093A && codepoint <= 0x093C)
        || (codepoint >= 0x093E && codepoint <= 0x094D)
        || (codepoint >= 0x0951 && codepoint <= 0x0957)
        || (codepoint >= 0x0962 && codepoint <= 0x0963)
        || (codepoint >= 0x0981 && codepoint <= 0x0983)
        || codepoint == 0x09BC
        || (codepoint >= 0x09BE && codepoint <= 0x09C4)
        || (codepoint >= 0x09C7 && codepoint <= 0x09C8)
        || (codepoint >= 0x09CB && codepoint <= 0x09CD)
        || codepoint == 0x09D7
        || (codepoint >= 0x09E2 && codepoint <= 0x09E3)
        || (codepoint >= 0x0A01 && codepoint <= 0x0A03)
        || codepoint == 0x0A3C
        || (codepoint >= 0x0A3E && codepoint <= 0x0A42)
        || (codepoint >= 0x0A47 && codepoint <= 0x0A48)
        || (codepoint >= 0x0A4B && codepoint <= 0x0A4D)
        || codepoint == 0x0A51
        || (codepoint >= 0x0A70 && codepoint <= 0x0A71)
        || codepoint == 0x0A75
        || (codepoint >= 0x0A81 && codepoint <= 0x0A83)
        || codepoint == 0x0ABC
        || (codepoint >= 0x0ABE && codepoint <= 0x0AC5)
        || (codepoint >= 0x0AC7 && codepoint <= 0x0AC9)
        || (codepoint >= 0x0ACB && codepoint <= 0x0ACD)
        || (codepoint >= 0x0AE2 && codepoint <= 0x0AE3)
        || (codepoint >= 0x0B01 && codepoint <= 0x0B03)
        || codepoint == 0x0B3C
        || (codepoint >= 0x0B3E && codepoint <= 0x0B44)
        || (codepoint >= 0x0B47 && codepoint <= 0x0B48)
        || (codepoint >= 0x0B4B && codepoint <= 0x0B4D)
        || (codepoint >= 0x0B55 && codepoint <= 0x0B57)
        || (codepoint >= 0x0B62 && codepoint <= 0x0B63)
        || codepoint == 0x0B82
        || (codepoint >= 0x0BBE && codepoint <= 0x0BC2)
        || (codepoint >= 0x0BC6 && codepoint <= 0x0BC8)
        || (codepoint >= 0x0BCA && codepoint <= 0x0BCD)
        || codepoint == 0x0BD7
        || (codepoint >= 0x0C00 && codepoint <= 0x0C04)
        || (codepoint >= 0x0C3E && codepoint <= 0x0C44)
        || (codepoint >= 0x0C46 && codepoint <= 0x0C48)
        || (codepoint >= 0x0C4A && codepoint <= 0x0C4D)
        || (codepoint >= 0x0C55 && codepoint <= 0x0C56)
        || (codepoint >= 0x0C62 && codepoint <= 0x0C63)
        || (codepoint >= 0x0C81 && codepoint <= 0x0C83)
        || codepoint == 0x0CBC
        || (codepoint >= 0x0CBE && codepoint <= 0x0CC4)
        || (codepoint >= 0x0CC6 && codepoint <= 0x0CC8)
        || (codepoint >= 0x0CCA && codepoint <= 0x0CCD)
        || (codepoint >= 0x0CD5 && codepoint <= 0x0CD6)
        || (codepoint >= 0x0CE2 && codepoint <= 0x0CE3)
        || (codepoint >= 0x0D00 && codepoint <= 0x0D03)
        || (codepoint >= 0x0D3B && codepoint <= 0x0D3C)
        || (codepoint >= 0x0D3E && codepoint <= 0x0D44)
        || (codepoint >= 0x0D46 && codepoint <= 0x0D48)
        || (codepoint >= 0x0D4A && codepoint <= 0x0D4D)
        || codepoint == 0x0D57
        || (codepoint >= 0x0D62 && codepoint <= 0x0D63)
        || codepoint == 0x0E31
        || (codepoint >= 0x0E34 && codepoint <= 0x0E3A)
        || (codepoint >= 0x0E47 && codepoint <= 0x0E4E)
        || (codepoint >= 0x0F18 && codepoint <= 0x0F19)
        || codepoint == 0x0F35 || codepoint == 0x0F37 || codepoint == 0x0F39
        || (codepoint >= 0x0F3E && codepoint <= 0x0F3F)
        || (codepoint >= 0x0F71 && codepoint <= 0x0F84)
        || (codepoint >= 0x0F86 && codepoint <= 0x0F87)
        || (codepoint >= 0x0F8D && codepoint <= 0x0FBC)
        || (codepoint >= 0x102B && codepoint <= 0x103E)
        || (codepoint >= 0x1056 && codepoint <= 0x1059)
        || (codepoint >= 0x105E && codepoint <= 0x1060)
        || (codepoint >= 0x1062 && codepoint <= 0x1064)
        || (codepoint >= 0x1067 && codepoint <= 0x106D)
        || (codepoint >= 0x1071 && codepoint <= 0x1074)
        || (codepoint >= 0x1082 && codepoint <= 0x108D)
        || codepoint == 0x108F
        || (codepoint >= 0x109A && codepoint <= 0x109D)
        || (codepoint >= 0x135D && codepoint <= 0x135F)
        || (codepoint >= 0x1712 && codepoint <= 0x1715)
        || (codepoint >= 0x17B4 && codepoint <= 0x17D3)
        || (codepoint >= 0x180B && codepoint <= 0x180F)
        || (codepoint >= 0x1A17 && codepoint <= 0x1A1B)
        || (codepoint >= 0x1A55 && codepoint <= 0x1A5E)
        || (codepoint >= 0x1A60 && codepoint <= 0x1A7C)
        || codepoint == 0x1A7F
        || (codepoint >= 0x1AB0 && codepoint <= 0x1AFF)
        || (codepoint >= 0x1B00 && codepoint <= 0x1B04)
        || (codepoint >= 0x1B34 && codepoint <= 0x1B44)
        || (codepoint >= 0x1B6B && codepoint <= 0x1B73)
        || (codepoint >= 0x1B80 && codepoint <= 0x1B82)
        || (codepoint >= 0x1BA1 && codepoint <= 0x1BAD)
        || (codepoint >= 0x1BE6 && codepoint <= 0x1BF3)
        || (codepoint >= 0x1C24 && codepoint <= 0x1C37)
        || (codepoint >= 0x1CD0 && codepoint <= 0x1CE8)
        || codepoint == 0x1CED || codepoint == 0x1CF4
        || (codepoint >= 0x1CF7 && codepoint <= 0x1CF9)
        || (codepoint >= 0x1DC0 && codepoint <= 0x1DFF)
        || (codepoint >= 0x20D0 && codepoint <= 0x20FF)
        || (codepoint >= 0xFE20 && codepoint <= 0xFE2F);
}

bool isVariationSelector(std::uint32_t codepoint)
{
    return (codepoint >= 0xFE00 && codepoint <= 0xFE0F)
        || (codepoint >= 0xE0100 && codepoint <= 0xE01EF);
}

bool isEmojiModifier(std::uint32_t codepoint)
{
    return codepoint >= 0x1F3FB && codepoint <= 0x1F3FF;
}

bool isRegionalIndicator(std::uint32_t codepoint)
{
    return codepoint >= 0x1F1E6 && codepoint <= 0x1F1FF;
}

bool isCjkCodepoint(std::uint32_t codepoint)
{
    return (codepoint >= 0x2E80 && codepoint <= 0x9FFF)
        || (codepoint >= 0xF900 && codepoint <= 0xFAFF)
        || (codepoint >= 0x20000 && codepoint <= 0x2FA1F)
        || (codepoint >= 0x3000 && codepoint <= 0x303F)
        || (codepoint >= 0xFF00 && codepoint <= 0xFFEF);
}

bool isClosingCjkPunctuation(std::uint32_t codepoint)
{
    switch (codepoint) {
    case 0x3001:
    case 0x3002:
    case 0x3009:
    case 0x300B:
    case 0x300D:
    case 0x300F:
    case 0x3011:
    case 0x3015:
    case 0x3017:
    case 0x3019:
    case 0x301B:
    case 0xFF01:
    case 0xFF09:
    case 0xFF0C:
    case 0xFF0E:
    case 0xFF1A:
    case 0xFF1B:
    case 0xFF1F:
    case 0xFF3D:
    case 0xFF5D:
        return true;
    default:
        return false;
    }
}

bool isOpeningCjkPunctuation(std::uint32_t codepoint)
{
    switch (codepoint) {
    case 0x3008:
    case 0x300A:
    case 0x300C:
    case 0x300E:
    case 0x3010:
    case 0x3014:
    case 0x3016:
    case 0x3018:
    case 0x301A:
    case 0xFF08:
    case 0xFF3B:
    case 0xFF5B:
        return true;
    default:
        return false;
    }
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

std::vector<FontFallbackCluster> buildFontFallbackClusters(const std::string &text,
                                                           std::size_t sourceStart,
                                                           std::size_t sourceEnd)
{
    std::vector<FontFallbackCluster> clusters;
    const std::size_t clampedEnd = std::min(sourceEnd, text.size());
    if (sourceStart >= clampedEnd) {
        return clusters;
    }

    std::uint32_t previous = 0;
    for (const Utf8Codepoint &codepoint : decodeUtf8(text)) {
        if (codepoint.offset < sourceStart) {
            continue;
        }
        if (codepoint.offset >= clampedEnd || isLineBreak(codepoint.value)) {
            break;
        }

        bool extend = !clusters.empty()
            && (isCombiningMark(codepoint.value)
                || isVariationSelector(codepoint.value)
                || isEmojiModifier(codepoint.value)
                || codepoint.value == 0x200D
                || previous == 0x200D);
        if (!extend && !clusters.empty() && isRegionalIndicator(codepoint.value)
            && isRegionalIndicator(previous)) {
            std::size_t regionalCount = 0;
            for (std::uint32_t value : clusters.back().codepoints) {
                regionalCount += isRegionalIndicator(value) ? 1u : 0u;
            }
            extend = (regionalCount % 2u) == 1u;
        }

        if (!extend) {
            FontFallbackCluster cluster;
            cluster.sourceStart = codepoint.offset;
            clusters.push_back(std::move(cluster));
        }
        FontFallbackCluster &cluster = clusters.back();
        cluster.sourceEnd = std::min(codepoint.offset + codepoint.length, clampedEnd);
        cluster.codepoints.push_back(codepoint.value);
        previous = codepoint.value;
    }
    return clusters;
}

bool isValidUtf8(const std::string &text)
{
    const auto codepoints = decodeUtf8(text);
    return std::all_of(codepoints.begin(), codepoints.end(), [](const Utf8Codepoint &codepoint) {
        return codepoint.valid;
    });
}

bool isZeroWidthBreakCodepoint(std::uint32_t codepoint)
{
    return codepoint == 0x200B;
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

        if (isZeroWidthBreakCodepoint(codepoint.value)) {
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

std::vector<TextBreakToken> buildTextBreakTokens(const std::string &text, std::size_t sourceStart,
                                                 std::size_t sourceEnd)
{
    std::vector<TextBreakToken> tokens;
    const std::size_t clampedEnd = std::min(sourceEnd, text.size());
    if (sourceStart >= clampedEnd) {
        return tokens;
    }

    const std::vector<Utf8Codepoint> codepoints = decodeUtf8(text);
    bool pendingSpace = false;
    std::size_t index = 0;
    while (index < codepoints.size()) {
        const Utf8Codepoint &codepoint = codepoints[index];
        if (codepoint.offset < sourceStart) {
            ++index;
            continue;
        }
        if (codepoint.offset >= clampedEnd || isLineBreak(codepoint.value)) {
            break;
        }
        if (isBreakWhitespace(codepoint.value)) {
            pendingSpace = !tokens.empty();
            ++index;
            continue;
        }
        if (isZeroWidthBreakCodepoint(codepoint.value)) {
            pendingSpace = false;
            ++index;
            continue;
        }

        TextBreakToken token;
        token.sourceStart = codepoint.offset;
        token.sourceEnd = std::min(codepoint.offset + codepoint.length, clampedEnd);
        token.prefixSpace = pendingSpace;
        pendingSpace = false;

        if (isCjkCodepoint(codepoint.value)) {
            if (isClosingCjkPunctuation(codepoint.value) && !tokens.empty()) {
                tokens.back().sourceEnd = token.sourceEnd;
            } else if (isOpeningCjkPunctuation(codepoint.value) && index + 1 < codepoints.size()) {
                const Utf8Codepoint &next = codepoints[index + 1];
                if (next.offset < clampedEnd && !isLineBreak(next.value) && !isBreakWhitespace(next.value)
                    && !isZeroWidthBreakCodepoint(next.value)) {
                    token.sourceEnd = std::min(next.offset + next.length, clampedEnd);
                    tokens.push_back(token);
                    index += 2;
                    continue;
                }
                tokens.push_back(token);
            } else {
                tokens.push_back(token);
            }
            ++index;
            continue;
        }

        ++index;
        while (index < codepoints.size()) {
            const Utf8Codepoint &next = codepoints[index];
            if (next.offset >= clampedEnd || isLineBreak(next.value) || isBreakWhitespace(next.value)
                || isZeroWidthBreakCodepoint(next.value) || isCjkCodepoint(next.value)) {
                break;
            }
            token.sourceEnd = std::min(next.offset + next.length, clampedEnd);
            ++index;
        }
        tokens.push_back(token);
    }

    return tokens;
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
    // Lay the glyphs out at the origin so only the local glyph coordinates are
    // scaled; the pen position (x, y) is a device-space translation applied
    // afterwards. Passing (x, y) into stb_easy_font_print would bake the origin
    // into the quad coordinates and then scale it too, throwing the text far off
    // its intended position for any non-zero origin.
    int quads = stb_easy_font_print(0.0f, 0.0f, const_cast<char *>(asciiText.c_str()), nullptr, buffer.data(), static_cast<int>(buffer.size()));
    if (quads <= 0) {
        return {};
    }

    const auto *quadData = reinterpret_cast<const float *>(buffer.data());
    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>(quads) * 12);

    for (int quad = 0; quad < quads; ++quad) {
        const float x0 = x + quadData[quad * 16 + 0] * scale;
        const float y0 = y + quadData[quad * 16 + 1] * scale;
        const float x1 = x + quadData[quad * 16 + 4] * scale;
        const float y1 = y + quadData[quad * 16 + 5] * scale;
        const float x2 = x + quadData[quad * 16 + 8] * scale;
        const float y2 = y + quadData[quad * 16 + 9] * scale;
        const float x3 = x + quadData[quad * 16 + 12] * scale;
        const float y3 = y + quadData[quad * 16 + 13] * scale;

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
