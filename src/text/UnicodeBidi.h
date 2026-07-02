#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wsc::text {

enum class BidiClass
{
    L,
    R,
    AL,
    EN,
    AN,
    ES,
    ET,
    CS,
    NSM,
    BN,
    B,
    S,
    WS,
    ON,
    LRE,
    RLE,
    LRO,
    RLO,
    PDF,
    LRI,
    RLI,
    FSI,
    PDI
};

struct BidiRun;

enum class BidiParagraphDirection
{
    Auto,
    LeftToRight,
    RightToLeft
};

BidiClass bidiClassForCodepoint(std::uint32_t codepoint);
std::vector<std::optional<int>> resolveUnicodeBidiLevelsForClasses(const std::vector<BidiClass> &classes,
                                                                   BidiParagraphDirection direction);
std::vector<std::optional<int>> resolveUnicodeBidiLevelsForCodepoints(const std::vector<std::uint32_t> &codepoints,
                                                                      BidiParagraphDirection direction);
std::vector<BidiRun> resolveUnicodeBidiRuns(const std::string &normalizedText);

} // namespace wsc::text
