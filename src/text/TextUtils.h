#pragma once

#include <string>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "canvas/Paint.h"

namespace wsc::text {

struct Utf8Codepoint
{
    std::uint32_t value = 0;
    std::size_t offset = 0;
    std::size_t length = 0;
    bool valid = false;
};

struct TextBreakToken
{
    std::size_t sourceStart = 0;
    std::size_t sourceEnd = 0;
    bool prefixSpace = false;
};

struct FontFallbackCluster
{
    std::size_t sourceStart = 0;
    std::size_t sourceEnd = 0;
    std::vector<std::uint32_t> codepoints;
};

enum class EmojiPresentation
{
    Default,
    Text,
    Emoji,
};

std::vector<Utf8Codepoint> decodeUtf8(const std::string &text);
std::vector<std::uint16_t> encodeCodepointsToUtf16(
    const std::vector<std::uint32_t> &codepoints);
std::vector<FontFallbackCluster> buildFontFallbackClusters(const std::string &text,
                                                           std::size_t sourceStart,
                                                           std::size_t sourceEnd);
bool isValidUtf8(const std::string &text);
std::string normalizeUtf8ForText(const std::string &text);
std::string makeAsciiFallbackText(const std::string &text, char replacement = '?');
std::size_t countUtf8Codepoints(const std::string &text);
bool isZeroWidthBreakCodepoint(std::uint32_t codepoint);
bool isVariationSelectorCodepoint(std::uint32_t codepoint);
EmojiPresentation classifyEmojiPresentation(
    const std::vector<std::uint32_t> &codepoints);
std::vector<TextBreakToken> buildTextBreakTokens(const std::string &text, std::size_t sourceStart,
                                                 std::size_t sourceEnd);
std::string sanitizeTextToAscii(const std::string &text);
float measureAsciiTextWidth(const std::string &asciiText, float scale, float letterSpacing);
float measureAsciiTextHeight(const std::string &asciiText, float scale);
float textBaselineOffset(Paint::TextBaseline baseline, float textHeight);
std::vector<float> buildTextVertices(const std::string &asciiText, float x, float y, float scale,
                                     float letterSpacing = 0.0f);

} // namespace wsc::text
