#include "text/UnicodeBidi.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "text/TextShaper.h"
#include "text/TextUtils.h"

namespace {

struct BidiItem
{
    wsc::text::Utf8Codepoint codepoint;
    wsc::text::BidiClass originalClass = wsc::text::BidiClass::ON;
    wsc::text::BidiClass resolvedClass = wsc::text::BidiClass::ON;
    int level = 0;
    bool visible = true;
};

struct DirectionState
{
    int level = 0;
    std::optional<wsc::text::BidiClass> overrideClass;
    bool isolate = false;
};

bool isStrongClass(wsc::text::BidiClass bidiClass)
{
    return bidiClass == wsc::text::BidiClass::L
        || bidiClass == wsc::text::BidiClass::R
        || bidiClass == wsc::text::BidiClass::AL;
}

bool isNumberClass(wsc::text::BidiClass bidiClass)
{
    return bidiClass == wsc::text::BidiClass::EN
        || bidiClass == wsc::text::BidiClass::AN;
}

bool isNeutralClass(wsc::text::BidiClass bidiClass)
{
    return bidiClass == wsc::text::BidiClass::B
        || bidiClass == wsc::text::BidiClass::S
        || bidiClass == wsc::text::BidiClass::WS
        || bidiClass == wsc::text::BidiClass::ON
        || bidiClass == wsc::text::BidiClass::BN
        || bidiClass == wsc::text::BidiClass::LRI
        || bidiClass == wsc::text::BidiClass::RLI
        || bidiClass == wsc::text::BidiClass::FSI
        || bidiClass == wsc::text::BidiClass::PDI;
}

bool isRemovedByX9(wsc::text::BidiClass bidiClass)
{
    return bidiClass == wsc::text::BidiClass::LRE
        || bidiClass == wsc::text::BidiClass::RLE
        || bidiClass == wsc::text::BidiClass::LRO
        || bidiClass == wsc::text::BidiClass::RLO
        || bidiClass == wsc::text::BidiClass::PDF
        || bidiClass == wsc::text::BidiClass::BN;
}

int nextEvenLevel(int level)
{
    const int candidate = level + 1;
    return candidate % 2 == 0 ? candidate : candidate + 1;
}

int nextOddLevel(int level)
{
    const int candidate = level + 1;
    return candidate % 2 == 1 ? candidate : candidate + 1;
}

std::optional<bool> firstStrongDirectionInIsolate(const std::vector<wsc::text::Utf8Codepoint> &codepoints,
                                                  std::size_t startIndex)
{
    int isolateDepth = 0;
    for (std::size_t index = startIndex + 1; index < codepoints.size(); ++index) {
        const wsc::text::BidiClass bidiClass = wsc::text::bidiClassForCodepoint(codepoints[index].value);
        if (bidiClass == wsc::text::BidiClass::B) {
            break;
        }
        if (bidiClass == wsc::text::BidiClass::LRI || bidiClass == wsc::text::BidiClass::RLI
            || bidiClass == wsc::text::BidiClass::FSI) {
            ++isolateDepth;
            continue;
        }
        if (bidiClass == wsc::text::BidiClass::PDI) {
            if (isolateDepth == 0) {
                break;
            }
            --isolateDepth;
            continue;
        }
        if (bidiClass == wsc::text::BidiClass::L) {
            return false;
        }
        if (bidiClass == wsc::text::BidiClass::R || bidiClass == wsc::text::BidiClass::AL) {
            return true;
        }
    }
    return std::nullopt;
}

int paragraphLevel(const std::vector<wsc::text::Utf8Codepoint> &codepoints)
{
    for (const wsc::text::Utf8Codepoint &codepoint : codepoints) {
        const wsc::text::BidiClass bidiClass = wsc::text::bidiClassForCodepoint(codepoint.value);
        if (bidiClass == wsc::text::BidiClass::B) {
            break;
        }
        if (bidiClass == wsc::text::BidiClass::L) {
            return 0;
        }
        if (bidiClass == wsc::text::BidiClass::R || bidiClass == wsc::text::BidiClass::AL) {
            return 1;
        }
    }
    return 0;
}

wsc::text::BidiClass sorClass(int paragraphLevelValue, int level)
{
    return (std::max(paragraphLevelValue, level) % 2) == 0
        ? wsc::text::BidiClass::L
        : wsc::text::BidiClass::R;
}

wsc::text::BidiClass previousStrongClass(const std::vector<BidiItem> &items, std::size_t index,
                                         int paragraphLevelValue)
{
    for (std::size_t cursor = index; cursor > 0; --cursor) {
        const wsc::text::BidiClass bidiClass = items[cursor - 1].resolvedClass;
        if (bidiClass == wsc::text::BidiClass::L || bidiClass == wsc::text::BidiClass::R
            || bidiClass == wsc::text::BidiClass::AL) {
            return bidiClass;
        }
    }
    return paragraphLevelValue % 2 == 0 ? wsc::text::BidiClass::L : wsc::text::BidiClass::R;
}

wsc::text::BidiClass previousStrongForNumbers(const std::vector<BidiItem> &items, std::size_t index,
                                              int paragraphLevelValue)
{
    for (std::size_t cursor = index; cursor > 0; --cursor) {
        const wsc::text::BidiClass bidiClass = items[cursor - 1].originalClass;
        if (bidiClass == wsc::text::BidiClass::L || bidiClass == wsc::text::BidiClass::R
            || bidiClass == wsc::text::BidiClass::AL) {
            return bidiClass;
        }
    }
    return paragraphLevelValue % 2 == 0 ? wsc::text::BidiClass::L : wsc::text::BidiClass::R;
}

wsc::text::BidiClass nextStrongClass(const std::vector<BidiItem> &items, std::size_t index,
                                     int paragraphLevelValue)
{
    for (std::size_t cursor = index + 1; cursor < items.size(); ++cursor) {
        const wsc::text::BidiClass bidiClass = items[cursor].resolvedClass;
        if (bidiClass == wsc::text::BidiClass::L || bidiClass == wsc::text::BidiClass::R) {
            return bidiClass;
        }
    }
    return paragraphLevelValue % 2 == 0 ? wsc::text::BidiClass::L : wsc::text::BidiClass::R;
}

void resolveWeakTypes(std::vector<BidiItem> &items, int paragraphLevelValue)
{
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].resolvedClass == wsc::text::BidiClass::NSM) {
            items[i].resolvedClass = i == 0 ? sorClass(paragraphLevelValue, items[i].level)
                                            : items[i - 1].resolvedClass;
        }
    }

    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].resolvedClass == wsc::text::BidiClass::EN
            && previousStrongForNumbers(items, i, paragraphLevelValue) == wsc::text::BidiClass::AL) {
            items[i].resolvedClass = wsc::text::BidiClass::AN;
        }
    }

    for (BidiItem &item : items) {
        if (item.resolvedClass == wsc::text::BidiClass::AL) {
            item.resolvedClass = wsc::text::BidiClass::R;
        }
    }

    for (std::size_t i = 1; i + 1 < items.size(); ++i) {
        const wsc::text::BidiClass prev = items[i - 1].resolvedClass;
        const wsc::text::BidiClass next = items[i + 1].resolvedClass;
        if (items[i].resolvedClass == wsc::text::BidiClass::ES
            && prev == wsc::text::BidiClass::EN && next == wsc::text::BidiClass::EN) {
            items[i].resolvedClass = wsc::text::BidiClass::EN;
        } else if (items[i].resolvedClass == wsc::text::BidiClass::CS
                   && prev == next && isNumberClass(prev)) {
            items[i].resolvedClass = prev;
        }
    }

    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].resolvedClass != wsc::text::BidiClass::ET) {
            continue;
        }
        std::size_t start = i;
        std::size_t end = i;
        while (start > 0 && items[start - 1].resolvedClass == wsc::text::BidiClass::ET) {
            --start;
        }
        while (end + 1 < items.size() && items[end + 1].resolvedClass == wsc::text::BidiClass::ET) {
            ++end;
        }
        const bool adjacentToEn = (start > 0 && items[start - 1].resolvedClass == wsc::text::BidiClass::EN)
            || (end + 1 < items.size() && items[end + 1].resolvedClass == wsc::text::BidiClass::EN);
        if (adjacentToEn) {
            for (std::size_t cursor = start; cursor <= end; ++cursor) {
                items[cursor].resolvedClass = wsc::text::BidiClass::EN;
            }
        }
        i = end;
    }

    for (BidiItem &item : items) {
        if (item.resolvedClass == wsc::text::BidiClass::ES
            || item.resolvedClass == wsc::text::BidiClass::ET
            || item.resolvedClass == wsc::text::BidiClass::CS) {
            item.resolvedClass = wsc::text::BidiClass::ON;
        }
    }

    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].resolvedClass == wsc::text::BidiClass::EN
            && previousStrongClass(items, i, paragraphLevelValue) == wsc::text::BidiClass::L) {
            items[i].resolvedClass = wsc::text::BidiClass::L;
        }
    }
}

void resolveNeutralTypes(std::vector<BidiItem> &items, int paragraphLevelValue)
{
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!isNeutralClass(items[i].resolvedClass)) {
            continue;
        }

        const std::size_t start = i;
        std::size_t end = i;
        while (end + 1 < items.size() && isNeutralClass(items[end + 1].resolvedClass)) {
            ++end;
        }

        const wsc::text::BidiClass before = previousStrongClass(items, start, paragraphLevelValue);
        const wsc::text::BidiClass after = nextStrongClass(items, end, paragraphLevelValue);
        const wsc::text::BidiClass resolved = before == after
            ? before
            : (items[start].level % 2 == 0 ? wsc::text::BidiClass::L : wsc::text::BidiClass::R);

        for (std::size_t cursor = start; cursor <= end; ++cursor) {
            items[cursor].resolvedClass = resolved;
        }
        i = end;
    }
}

void resolveImplicitLevels(std::vector<BidiItem> &items)
{
    for (BidiItem &item : items) {
        if (!item.visible) {
            continue;
        }
        if (item.level % 2 == 0) {
            if (item.resolvedClass == wsc::text::BidiClass::R) {
                item.level += 1;
            } else if (item.resolvedClass == wsc::text::BidiClass::AN
                       || item.resolvedClass == wsc::text::BidiClass::EN) {
                item.level += 2;
            }
        } else if (item.resolvedClass == wsc::text::BidiClass::L
                   || item.resolvedClass == wsc::text::BidiClass::AN
                   || item.resolvedClass == wsc::text::BidiClass::EN) {
            item.level += 1;
        }
    }
}

} // namespace

namespace wsc::text {

BidiClass bidiClassForCodepoint(std::uint32_t codepoint)
{
    if (codepoint == '\n' || codepoint == '\r' || codepoint == 0x2029) {
        return BidiClass::B;
    }
    if (codepoint == '\t' || codepoint == 0x000B) {
        return BidiClass::S;
    }
    if (codepoint == ' ' || codepoint == 0x00A0 || codepoint == 0x1680
        || (codepoint >= 0x2000 && codepoint <= 0x200A)
        || codepoint == 0x2028 || codepoint == 0x205F || codepoint == 0x3000) {
        return BidiClass::WS;
    }
    switch (codepoint) {
    case 0x061C: return BidiClass::AL;
    case 0x200E: return BidiClass::L;
    case 0x200F: return BidiClass::R;
    case 0x202A: return BidiClass::LRE;
    case 0x202B: return BidiClass::RLE;
    case 0x202C: return BidiClass::PDF;
    case 0x202D: return BidiClass::LRO;
    case 0x202E: return BidiClass::RLO;
    case 0x2066: return BidiClass::LRI;
    case 0x2067: return BidiClass::RLI;
    case 0x2068: return BidiClass::FSI;
    case 0x2069: return BidiClass::PDI;
    default: break;
    }
    if (codepoint < 0x20 || codepoint == 0x00AD || codepoint == 0x034F
        || (codepoint >= 0x200B && codepoint <= 0x200D)
        || (codepoint >= 0x2060 && codepoint <= 0x2064)
        || (codepoint >= 0xFE00 && codepoint <= 0xFE0F)) {
        return BidiClass::BN;
    }
    if ((codepoint >= 0x0300 && codepoint <= 0x036F)
        || (codepoint >= 0x1AB0 && codepoint <= 0x1AFF)
        || (codepoint >= 0x1DC0 && codepoint <= 0x1DFF)
        || (codepoint >= 0x20D0 && codepoint <= 0x20FF)
        || (codepoint >= 0xFE20 && codepoint <= 0xFE2F)) {
        return BidiClass::NSM;
    }
    if (codepoint >= '0' && codepoint <= '9') {
        return BidiClass::EN;
    }
    if (codepoint >= 0x0660 && codepoint <= 0x0669) {
        return BidiClass::AN;
    }
    if (codepoint == '+' || codepoint == '-') {
        return BidiClass::ES;
    }
    if (codepoint == ',' || codepoint == '.' || codepoint == '/' || codepoint == ':'
        || codepoint == 0x066B || codepoint == 0x066C) {
        return BidiClass::CS;
    }
    if (codepoint == '$' || codepoint == '#' || codepoint == '%' || codepoint == 0x00A2
        || codepoint == 0x00A3 || codepoint == 0x00A5 || codepoint == 0x20AC) {
        return BidiClass::ET;
    }
    if ((codepoint >= 0x0600 && codepoint <= 0x07BF)
        || (codepoint >= 0x08A0 && codepoint <= 0x08FF)
        || (codepoint >= 0xFB50 && codepoint <= 0xFDFF)
        || (codepoint >= 0xFE70 && codepoint <= 0xFEFF)) {
        return BidiClass::AL;
    }
    if ((codepoint >= 0x0590 && codepoint <= 0x05FF)
        || (codepoint >= 0x07C0 && codepoint <= 0x089F)) {
        return BidiClass::R;
    }
    if ((codepoint >= 'A' && codepoint <= 'Z')
        || (codepoint >= 'a' && codepoint <= 'z')
        || (codepoint >= 0x00C0 && codepoint <= 0x02AF)
        || (codepoint >= 0x0370 && codepoint <= 0x058F)
        || (codepoint >= 0x0900 && codepoint <= 0x1FFF)
        || (codepoint >= 0x2C00 && codepoint <= 0xD7FF)) {
        return BidiClass::L;
    }
    return BidiClass::ON;
}

std::vector<BidiRun> resolveUnicodeBidiRuns(const std::string &normalizedText)
{
    const std::vector<Utf8Codepoint> codepoints = decodeUtf8(normalizedText);
    const int baseLevel = paragraphLevel(codepoints);
    std::vector<DirectionState> stack = {{baseLevel, std::nullopt, false}};
    std::vector<BidiItem> items;

    for (std::size_t index = 0; index < codepoints.size(); ++index) {
        const Utf8Codepoint &codepoint = codepoints[index];
        BidiClass bidiClass = bidiClassForCodepoint(codepoint.value);
        if (bidiClass == BidiClass::B) {
            break;
        }

        DirectionState current = stack.back();
        auto pushState = [&](int level, std::optional<BidiClass> overrideClass, bool isolate) {
            if (level <= 125) {
                stack.push_back({level, overrideClass, isolate});
            }
        };

        if (bidiClass == BidiClass::LRE) {
            pushState(nextEvenLevel(current.level), std::nullopt, false);
        } else if (bidiClass == BidiClass::RLE) {
            pushState(nextOddLevel(current.level), std::nullopt, false);
        } else if (bidiClass == BidiClass::LRO) {
            pushState(nextEvenLevel(current.level), BidiClass::L, false);
        } else if (bidiClass == BidiClass::RLO) {
            pushState(nextOddLevel(current.level), BidiClass::R, false);
        } else if (bidiClass == BidiClass::LRI || bidiClass == BidiClass::RLI || bidiClass == BidiClass::FSI) {
            bool rtl = bidiClass == BidiClass::RLI;
            if (bidiClass == BidiClass::FSI) {
                rtl = firstStrongDirectionInIsolate(codepoints, index).value_or(false);
            }
            pushState(rtl ? nextOddLevel(current.level) : nextEvenLevel(current.level), std::nullopt, true);
        } else if (bidiClass == BidiClass::PDF) {
            if (stack.size() > 1 && !stack.back().isolate) {
                stack.pop_back();
            }
        } else if (bidiClass == BidiClass::PDI) {
            while (stack.size() > 1) {
                const bool wasIsolate = stack.back().isolate;
                stack.pop_back();
                if (wasIsolate) {
                    break;
                }
            }
        }

        current = stack.back();
        BidiItem item;
        item.codepoint = codepoint;
        item.originalClass = bidiClass;
        item.resolvedClass = current.overrideClass.value_or(bidiClass);
        item.level = current.level;
        item.visible = !isRemovedByX9(bidiClass)
            && codepoint.value != 0x061C
            && codepoint.value != 0x200E
            && codepoint.value != 0x200F
            && bidiClass != BidiClass::LRI
            && bidiClass != BidiClass::RLI
            && bidiClass != BidiClass::FSI
            && bidiClass != BidiClass::PDI;
        if (item.visible) {
            items.push_back(item);
        }
    }

    items.erase(std::remove_if(items.begin(), items.end(), [](const BidiItem &item) {
        return item.codepoint.value < 32 || item.originalClass == BidiClass::BN;
    }), items.end());

    if (items.empty()) {
        return {};
    }

    resolveWeakTypes(items, baseLevel);
    resolveNeutralTypes(items, baseLevel);
    resolveImplicitLevels(items);

    std::vector<BidiRun> logicalRuns;
    std::size_t runStart = 0;
    for (std::size_t i = 1; i <= items.size(); ++i) {
        if (i < items.size() && items[i].level == items[runStart].level) {
            continue;
        }
        logicalRuns.push_back({items[runStart].codepoint.offset,
                               items[i - 1].codepoint.offset + items[i - 1].codepoint.length,
                               items[runStart].level % 2 == 1});
        runStart = i;
    }

    if (baseLevel % 2 == 1) {
        std::reverse(logicalRuns.begin(), logicalRuns.end());
    }
    return logicalRuns;
}

} // namespace wsc::text
