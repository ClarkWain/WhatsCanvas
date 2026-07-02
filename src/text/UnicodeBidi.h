#pragma once

#include <cstddef>
#include <cstdint>
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

BidiClass bidiClassForCodepoint(std::uint32_t codepoint);
std::vector<BidiRun> resolveUnicodeBidiRuns(const std::string &normalizedText);

} // namespace wsc::text
