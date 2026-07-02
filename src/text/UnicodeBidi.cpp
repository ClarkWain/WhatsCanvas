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
    std::size_t sourceIndex = 0;
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

#include "text/UnicodeBidiData.inc"

std::optional<wsc::text::BidiClass> lookupDerivedBidiClass(std::uint32_t codepoint)
{
    for (const BidiClassRange &range : kDerivedBidiClassRanges) {
        if (codepoint >= range.first && codepoint <= range.last) {
            return range.bidiClass;
        }
    }
    for (const BidiClassRange &range : kDerivedBidiClassMissingRanges) {
        if (codepoint >= range.first && codepoint <= range.last) {
            return range.bidiClass;
        }
    }
    return wsc::text::BidiClass::L;
}

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

bool isDirectionalIsolate(wsc::text::BidiClass bidiClass)
{
    return bidiClass == wsc::text::BidiClass::LRI
        || bidiClass == wsc::text::BidiClass::RLI
        || bidiClass == wsc::text::BidiClass::FSI
        || bidiClass == wsc::text::BidiClass::PDI;
}

bool isInvisibleFormatting(wsc::text::BidiClass bidiClass, std::uint32_t codepoint)
{
    (void)codepoint;
    return isRemovedByX9(bidiClass);
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

std::optional<bool> firstStrongDirectionInIsolate(const std::vector<wsc::text::BidiClass> &classes,
                                                  std::size_t startIndex)
{
    int isolateDepth = 0;
    for (std::size_t index = startIndex + 1; index < classes.size(); ++index) {
        const wsc::text::BidiClass bidiClass = classes[index];
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

int paragraphLevel(const std::vector<wsc::text::Utf8Codepoint> &codepoints,
                   wsc::text::BidiParagraphDirection direction)
{
    if (direction == wsc::text::BidiParagraphDirection::LeftToRight) {
        return 0;
    }
    if (direction == wsc::text::BidiParagraphDirection::RightToLeft) {
        return 1;
    }
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

int paragraphLevel(const std::vector<wsc::text::BidiClass> &classes,
                   wsc::text::BidiParagraphDirection direction)
{
    if (direction == wsc::text::BidiParagraphDirection::LeftToRight) {
        return 0;
    }
    if (direction == wsc::text::BidiParagraphDirection::RightToLeft) {
        return 1;
    }
    for (const wsc::text::BidiClass bidiClass : classes) {
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

wsc::text::BidiClass neutralBoundaryClass(wsc::text::BidiClass bidiClass)
{
    if (bidiClass == wsc::text::BidiClass::AN || bidiClass == wsc::text::BidiClass::EN) {
        return wsc::text::BidiClass::R;
    }
    return bidiClass;
}

wsc::text::BidiClass previousNeutralBoundaryClass(const std::vector<BidiItem> &items, std::size_t index,
                                                  int paragraphLevelValue)
{
    for (std::size_t cursor = index; cursor > 0; --cursor) {
        const wsc::text::BidiClass bidiClass = neutralBoundaryClass(items[cursor - 1].resolvedClass);
        if (bidiClass == wsc::text::BidiClass::L || bidiClass == wsc::text::BidiClass::R) {
            return bidiClass;
        }
    }
    return paragraphLevelValue % 2 == 0 ? wsc::text::BidiClass::L : wsc::text::BidiClass::R;
}

wsc::text::BidiClass nextNeutralBoundaryClass(const std::vector<BidiItem> &items, std::size_t index,
                                              int paragraphLevelValue)
{
    for (std::size_t cursor = index + 1; cursor < items.size(); ++cursor) {
        const wsc::text::BidiClass bidiClass = neutralBoundaryClass(items[cursor].resolvedClass);
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

        const wsc::text::BidiClass before = previousNeutralBoundaryClass(items, start, paragraphLevelValue);
        const wsc::text::BidiClass after = nextNeutralBoundaryClass(items, end, paragraphLevelValue);
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

void resolveLineBreakLevels(std::vector<BidiItem> &items, int paragraphLevelValue)
{
    for (BidiItem &item : items) {
        if (item.originalClass == wsc::text::BidiClass::B
            || item.originalClass == wsc::text::BidiClass::S) {
            item.level = paragraphLevelValue;
        }
    }

    for (std::size_t cursor = items.size(); cursor > 0; --cursor) {
        BidiItem &item = items[cursor - 1];
        if (item.originalClass == wsc::text::BidiClass::WS
            || item.originalClass == wsc::text::BidiClass::BN) {
            item.level = paragraphLevelValue;
            continue;
        }
        break;
    }

    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].originalClass != wsc::text::BidiClass::B
            && items[i].originalClass != wsc::text::BidiClass::S) {
            continue;
        }
        for (std::size_t cursor = i; cursor > 0; --cursor) {
            BidiItem &item = items[cursor - 1];
            if (item.originalClass == wsc::text::BidiClass::WS
                || item.originalClass == wsc::text::BidiClass::BN) {
                item.level = paragraphLevelValue;
                continue;
            }
            break;
        }
    }
}

std::vector<BidiItem> resolveItemsFromClasses(const std::vector<wsc::text::BidiClass> &classes,
                                              const std::vector<wsc::text::Utf8Codepoint> &codepoints,
                                              wsc::text::BidiParagraphDirection direction)
{
    const int baseLevel = paragraphLevel(classes, direction);
    std::vector<DirectionState> stack = {{baseLevel, std::nullopt, false}};
    std::vector<BidiItem> allItems;

    for (std::size_t index = 0; index < classes.size(); ++index) {
        const wsc::text::BidiClass bidiClass = classes[index];

        DirectionState current = stack.back();
        int itemLevel = current.level;
        std::optional<wsc::text::BidiClass> itemOverride = current.overrideClass;
        auto pushState = [&](int level, std::optional<wsc::text::BidiClass> overrideClass, bool isolate) {
            if (level <= 125) {
                stack.push_back({level, overrideClass, isolate});
            }
        };

        if (bidiClass == wsc::text::BidiClass::LRE) {
            pushState(nextEvenLevel(current.level), std::nullopt, false);
        } else if (bidiClass == wsc::text::BidiClass::RLE) {
            pushState(nextOddLevel(current.level), std::nullopt, false);
        } else if (bidiClass == wsc::text::BidiClass::LRO) {
            pushState(nextEvenLevel(current.level), wsc::text::BidiClass::L, false);
        } else if (bidiClass == wsc::text::BidiClass::RLO) {
            pushState(nextOddLevel(current.level), wsc::text::BidiClass::R, false);
        } else if (bidiClass == wsc::text::BidiClass::LRI
                   || bidiClass == wsc::text::BidiClass::RLI
                   || bidiClass == wsc::text::BidiClass::FSI) {
            bool rtl = bidiClass == wsc::text::BidiClass::RLI;
            if (bidiClass == wsc::text::BidiClass::FSI) {
                rtl = firstStrongDirectionInIsolate(classes, index).value_or(false);
            }
            pushState(rtl ? nextOddLevel(current.level) : nextEvenLevel(current.level), std::nullopt, true);
            itemOverride = std::nullopt;
        } else if (bidiClass == wsc::text::BidiClass::PDF) {
            if (stack.size() > 1 && !stack.back().isolate) {
                stack.pop_back();
            }
        } else if (bidiClass == wsc::text::BidiClass::PDI) {
            while (stack.size() > 1) {
                const bool wasIsolate = stack.back().isolate;
                stack.pop_back();
                if (wasIsolate) {
                    break;
                }
            }
            itemLevel = stack.back().level;
            itemOverride = std::nullopt;
        } else {
            current = stack.back();
            itemLevel = current.level;
            itemOverride = current.overrideClass;
        }

        BidiItem item;
        item.sourceIndex = index;
        item.codepoint = codepoints.empty()
            ? wsc::text::Utf8Codepoint{static_cast<std::uint32_t>(index), index, 1, true}
            : codepoints[index];
        item.originalClass = bidiClass;
        item.resolvedClass = itemOverride.value_or(bidiClass);
        item.level = itemLevel;
        item.visible = !isInvisibleFormatting(bidiClass, item.codepoint.value);
        allItems.push_back(item);
        if (bidiClass == wsc::text::BidiClass::B) {
            break;
        }
    }

    std::vector<BidiItem> visibleItems;
    visibleItems.reserve(allItems.size());
    for (const BidiItem &item : allItems) {
        if (item.visible) {
            visibleItems.push_back(item);
        }
    }

    if (!visibleItems.empty()) {
        resolveWeakTypes(visibleItems, baseLevel);
        resolveNeutralTypes(visibleItems, baseLevel);
        resolveImplicitLevels(visibleItems);
        resolveLineBreakLevels(visibleItems, baseLevel);
    }

    for (const BidiItem &visibleItem : visibleItems) {
        for (BidiItem &item : allItems) {
            if (item.sourceIndex == visibleItem.sourceIndex) {
                item.resolvedClass = visibleItem.resolvedClass;
                item.level = visibleItem.level;
                break;
            }
        }
    }
    return allItems;
}

} // namespace

namespace wsc::text {

BidiClass bidiClassForCodepoint(std::uint32_t codepoint)
{
    return lookupDerivedBidiClass(codepoint).value_or(BidiClass::L);
}

std::vector<std::optional<int>> resolveUnicodeBidiLevelsForClasses(const std::vector<BidiClass> &classes,
                                                                   BidiParagraphDirection direction)
{
    const std::vector<BidiItem> items = resolveItemsFromClasses(classes, {}, direction);
    std::vector<std::optional<int>> levels(classes.size(), std::nullopt);
    for (const BidiItem &item : items) {
        if (item.visible) {
            levels[item.sourceIndex] = item.level;
        }
    }
    return levels;
}

std::vector<std::optional<int>> resolveUnicodeBidiLevelsForCodepoints(const std::vector<std::uint32_t> &codepoints,
                                                                      BidiParagraphDirection direction)
{
    std::vector<Utf8Codepoint> decoded;
    decoded.reserve(codepoints.size());
    std::vector<BidiClass> classes;
    classes.reserve(codepoints.size());
    for (std::size_t i = 0; i < codepoints.size(); ++i) {
        decoded.push_back({codepoints[i], i, 1, true});
        classes.push_back(bidiClassForCodepoint(codepoints[i]));
    }

    const std::vector<BidiItem> items = resolveItemsFromClasses(classes, decoded, direction);
    std::vector<std::optional<int>> levels(codepoints.size(), std::nullopt);
    for (const BidiItem &item : items) {
        if (item.visible) {
            levels[item.sourceIndex] = item.level;
        }
    }
    return levels;
}

std::vector<BidiRun> resolveUnicodeBidiRuns(const std::string &normalizedText)
{
    const std::vector<Utf8Codepoint> codepoints = decodeUtf8(normalizedText);
    std::vector<BidiClass> classes;
    classes.reserve(codepoints.size());
    for (const Utf8Codepoint &codepoint : codepoints) {
        classes.push_back(bidiClassForCodepoint(codepoint.value));
    }
    std::vector<BidiItem> allItems = resolveItemsFromClasses(classes, codepoints, BidiParagraphDirection::Auto);
    std::vector<BidiItem> items;
    items.reserve(allItems.size());
    for (const BidiItem &item : allItems) {
        if (item.visible
            && item.originalClass != BidiClass::B
            && item.originalClass != BidiClass::S
            && !isBidiControlCodepoint(item.codepoint.value)) {
            items.push_back(item);
        }
    }

    if (items.empty()) {
        return {};
    }

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

    if (paragraphLevel(classes, BidiParagraphDirection::Auto) % 2 == 1) {
        std::reverse(logicalRuns.begin(), logicalRuns.end());
    }
    return logicalRuns;
}

} // namespace wsc::text
