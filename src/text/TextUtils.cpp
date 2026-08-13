#include "text/TextUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>

#include "stb_easy_font.h"
#include "text/GraphemeBreakData.h"

namespace {

namespace grapheme_data = wsc::text::grapheme_data;

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

template <typename Range, std::size_t N>
bool containsCodepoint(const Range (&ranges)[N], std::uint32_t codepoint)
{
    std::size_t first = 0;
    std::size_t last = N;
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if (codepoint < ranges[middle].first) {
            last = middle;
        } else if (codepoint > ranges[middle].last) {
            first = middle + 1;
        } else {
            return true;
        }
    }
    return false;
}

grapheme_data::BreakProperty graphemeBreakProperty(std::uint32_t codepoint)
{
    if (codepoint >= 0xAC00 && codepoint <= 0xD7A3) {
        return ((codepoint - 0xAC00) % 28u) == 0u
            ? grapheme_data::BreakProperty::LV
            : grapheme_data::BreakProperty::LVT;
    }

    const auto &ranges = grapheme_data::kBreakPropertyRanges;
    std::size_t first = 0;
    std::size_t last = std::size(ranges);
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if (codepoint < ranges[middle].first) {
            last = middle;
        } else if (codepoint > ranges[middle].last) {
            first = middle + 1;
        } else {
            return ranges[middle].property;
        }
    }
    return grapheme_data::BreakProperty::Other;
}

bool isExtendedPictographic(std::uint32_t codepoint)
{
    return containsCodepoint(grapheme_data::kExtendedPictographicRanges, codepoint);
}

bool isInCbConsonant(std::uint32_t codepoint)
{
    return containsCodepoint(grapheme_data::kInCbConsonantRanges, codepoint);
}

bool isInCbExtend(std::uint32_t codepoint)
{
    return containsCodepoint(grapheme_data::kInCbExtendRanges, codepoint);
}

bool isInCbLinker(std::uint32_t codepoint)
{
    return containsCodepoint(grapheme_data::kInCbLinkerRanges, codepoint);
}

bool shouldBreakGrapheme(const std::vector<wsc::text::Utf8Codepoint> &codepoints,
                         std::size_t index)
{
    using Property = grapheme_data::BreakProperty;
    if (index == 0 || index >= codepoints.size()) {
        return true; // GB1 / GB2
    }

    const Property previous = graphemeBreakProperty(codepoints[index - 1].value);
    const Property current = graphemeBreakProperty(codepoints[index].value);
    if (previous == Property::CR && current == Property::LF) {
        return false; // GB3
    }
    const auto isControl = [](Property property) {
        return property == Property::Control || property == Property::CR || property == Property::LF;
    };
    if (isControl(previous) || isControl(current)) {
        return true; // GB4 / GB5
    }
    if (previous == Property::L
        && (current == Property::L || current == Property::V
            || current == Property::LV || current == Property::LVT)) {
        return false; // GB6
    }
    if ((previous == Property::LV || previous == Property::V)
        && (current == Property::V || current == Property::T)) {
        return false; // GB7
    }
    if ((previous == Property::LVT || previous == Property::T) && current == Property::T) {
        return false; // GB8
    }
    if (current == Property::Extend || current == Property::ZWJ
        || current == Property::SpacingMark) {
        return false; // GB9 / GB9a
    }
    if (previous == Property::Prepend) {
        return false; // GB9b
    }

    // GB9c: InCB=Consonant (InCB=Extend|Linker)* InCB=Linker
    //       (InCB=Extend|Linker)* x InCB=Consonant.
    if (isInCbConsonant(codepoints[index].value)) {
        bool sawLinker = false;
        for (std::size_t cursor = index; cursor > 0;) {
            const std::uint32_t value = codepoints[--cursor].value;
            if (isInCbLinker(value)) {
                sawLinker = true;
                continue;
            }
            if (isInCbExtend(value)) {
                continue;
            }
            if (sawLinker && isInCbConsonant(value)) {
                return false;
            }
            break;
        }
    }

    // GB11: Extended_Pictographic Extend* ZWJ x Extended_Pictographic.
    if (previous == Property::ZWJ && isExtendedPictographic(codepoints[index].value)) {
        std::size_t cursor = index - 1;
        while (cursor > 0
               && graphemeBreakProperty(codepoints[cursor - 1].value) == Property::Extend) {
            --cursor;
        }
        if (cursor > 0 && isExtendedPictographic(codepoints[cursor - 1].value)) {
            return false;
        }
    }

    // GB12 / GB13: pair regional indicators from the start of each RI run.
    if (previous == Property::RegionalIndicator && current == Property::RegionalIndicator) {
        std::size_t precedingRegionalIndicators = 0;
        for (std::size_t cursor = index; cursor > 0; --cursor) {
            if (graphemeBreakProperty(codepoints[cursor - 1].value) != Property::RegionalIndicator) {
                break;
            }
            ++precedingRegionalIndicators;
        }
        if ((precedingRegionalIndicators % 2u) == 1u) {
            return false;
        }
    }
    return true; // GB999
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

    std::vector<Utf8Codepoint> codepoints;
    for (const Utf8Codepoint &codepoint : decodeUtf8(text)) {
        if (codepoint.offset < sourceStart) {
            continue;
        }
        if (codepoint.offset >= clampedEnd || isLineBreak(codepoint.value)) {
            break;
        }
        codepoints.push_back(codepoint);
    }

    for (std::size_t index = 0; index < codepoints.size(); ++index) {
        const Utf8Codepoint &codepoint = codepoints[index];
        if (shouldBreakGrapheme(codepoints, index)) {
            FontFallbackCluster cluster;
            cluster.sourceStart = codepoint.offset;
            clusters.push_back(std::move(cluster));
        }
        FontFallbackCluster &cluster = clusters.back();
        cluster.sourceEnd = std::min(codepoint.offset + codepoint.length, clampedEnd);
        cluster.codepoints.push_back(codepoint.value);
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

    const std::vector<FontFallbackCluster> clusters =
        buildFontFallbackClusters(text, sourceStart, clampedEnd);
    bool pendingSpace = false;
    std::size_t index = 0;
    while (index < clusters.size()) {
        const FontFallbackCluster &cluster = clusters[index];
        if (cluster.codepoints.empty()) {
            ++index;
            continue;
        }
        const std::uint32_t firstCodepoint = cluster.codepoints.front();
        if (isBreakWhitespace(firstCodepoint)) {
            pendingSpace = !tokens.empty();
            ++index;
            continue;
        }
        if (isZeroWidthBreakCodepoint(firstCodepoint)) {
            pendingSpace = false;
            ++index;
            continue;
        }

        TextBreakToken token;
        token.sourceStart = cluster.sourceStart;
        token.sourceEnd = cluster.sourceEnd;
        token.prefixSpace = pendingSpace;
        pendingSpace = false;

        if (isCjkCodepoint(firstCodepoint)) {
            if (isClosingCjkPunctuation(firstCodepoint) && !tokens.empty()) {
                tokens.back().sourceEnd = token.sourceEnd;
            } else if (isOpeningCjkPunctuation(firstCodepoint) && index + 1 < clusters.size()) {
                const FontFallbackCluster &next = clusters[index + 1];
                const std::uint32_t nextFirst = next.codepoints.empty() ? 0 : next.codepoints.front();
                if (!next.codepoints.empty() && !isBreakWhitespace(nextFirst)
                    && !isZeroWidthBreakCodepoint(nextFirst)) {
                    token.sourceEnd = next.sourceEnd;
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
        while (index < clusters.size()) {
            const FontFallbackCluster &next = clusters[index];
            if (next.codepoints.empty()) {
                ++index;
                continue;
            }
            const std::uint32_t nextFirst = next.codepoints.front();
            if (isBreakWhitespace(nextFirst) || isZeroWidthBreakCodepoint(nextFirst)
                || isCjkCodepoint(nextFirst)) {
                break;
            }
            token.sourceEnd = next.sourceEnd;
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
