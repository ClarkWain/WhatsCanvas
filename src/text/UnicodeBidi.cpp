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
    bool preserveLevel = false;
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

bool isIsolateInitiator(wsc::text::BidiClass bidiClass)
{
    return bidiClass == wsc::text::BidiClass::LRI
        || bidiClass == wsc::text::BidiClass::RLI
        || bidiClass == wsc::text::BidiClass::FSI;
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
        if (isolateDepth > 0) {
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
        if (isolateDepth > 0) {
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
    int isolateDepth = 0;
    for (const wsc::text::Utf8Codepoint &codepoint : codepoints) {
        const wsc::text::BidiClass bidiClass = wsc::text::bidiClassForCodepoint(codepoint.value);
        if (bidiClass == wsc::text::BidiClass::B) {
            break;
        }
        if (bidiClass == wsc::text::BidiClass::LRI || bidiClass == wsc::text::BidiClass::RLI
            || bidiClass == wsc::text::BidiClass::FSI) {
            ++isolateDepth;
            continue;
        }
        if (bidiClass == wsc::text::BidiClass::PDI) {
            if (isolateDepth > 0) {
                --isolateDepth;
            }
            continue;
        }
        if (isolateDepth > 0) {
            continue;
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
    int isolateDepth = 0;
    for (const wsc::text::BidiClass bidiClass : classes) {
        if (bidiClass == wsc::text::BidiClass::B) {
            break;
        }
        if (bidiClass == wsc::text::BidiClass::LRI || bidiClass == wsc::text::BidiClass::RLI
            || bidiClass == wsc::text::BidiClass::FSI) {
            ++isolateDepth;
            continue;
        }
        if (bidiClass == wsc::text::BidiClass::PDI) {
            if (isolateDepth > 0) {
                --isolateDepth;
            }
            continue;
        }
        if (isolateDepth > 0) {
            continue;
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
                                         wsc::text::BidiClass startClass)
{
    for (std::size_t cursor = index; cursor > 0; --cursor) {
        const wsc::text::BidiClass bidiClass = items[cursor - 1].resolvedClass;
        if (bidiClass == wsc::text::BidiClass::L || bidiClass == wsc::text::BidiClass::R
            || bidiClass == wsc::text::BidiClass::AL) {
            return bidiClass;
        }
    }
    return startClass;
}

wsc::text::BidiClass previousStrongForNumbers(const std::vector<BidiItem> &items, std::size_t index,
                                              wsc::text::BidiClass startClass)
{
    for (std::size_t cursor = index; cursor > 0; --cursor) {
        const wsc::text::BidiClass bidiClass = items[cursor - 1].originalClass;
        if (bidiClass == wsc::text::BidiClass::L || bidiClass == wsc::text::BidiClass::R
            || bidiClass == wsc::text::BidiClass::AL) {
            return bidiClass;
        }
    }
    return startClass;
}

wsc::text::BidiClass nextStrongClass(const std::vector<BidiItem> &items, std::size_t index,
                                     wsc::text::BidiClass endClass)
{
    for (std::size_t cursor = index + 1; cursor < items.size(); ++cursor) {
        const wsc::text::BidiClass bidiClass = items[cursor].resolvedClass;
        if (bidiClass == wsc::text::BidiClass::L || bidiClass == wsc::text::BidiClass::R) {
            return bidiClass;
        }
    }
    return endClass;
}

wsc::text::BidiClass neutralBoundaryClass(wsc::text::BidiClass bidiClass)
{
    if (bidiClass == wsc::text::BidiClass::AN || bidiClass == wsc::text::BidiClass::EN) {
        return wsc::text::BidiClass::R;
    }
    return bidiClass;
}

wsc::text::BidiClass previousNeutralBoundaryClass(const std::vector<BidiItem> &items, std::size_t index,
                                                  wsc::text::BidiClass startClass)
{
    for (std::size_t cursor = index; cursor > 0; --cursor) {
        const wsc::text::BidiClass bidiClass = neutralBoundaryClass(items[cursor - 1].resolvedClass);
        if (bidiClass == wsc::text::BidiClass::L || bidiClass == wsc::text::BidiClass::R) {
            return bidiClass;
        }
    }
    return startClass;
}

wsc::text::BidiClass nextNeutralBoundaryClass(const std::vector<BidiItem> &items, std::size_t index,
                                              wsc::text::BidiClass endClass)
{
    for (std::size_t cursor = index + 1; cursor < items.size(); ++cursor) {
        const wsc::text::BidiClass bidiClass = neutralBoundaryClass(items[cursor].resolvedClass);
        if (bidiClass == wsc::text::BidiClass::L || bidiClass == wsc::text::BidiClass::R) {
            return bidiClass;
        }
    }
    return endClass;
}

const BidiBracketEntry *lookupBidiBracket(std::uint32_t codepoint)
{
    for (const BidiBracketEntry &entry : kBidiBracketEntries) {
        if (entry.codepoint == codepoint) {
            return &entry;
        }
    }
    return nullptr;
}

wsc::text::BidiClass bracketStrongClass(wsc::text::BidiClass bidiClass)
{
    if (bidiClass == wsc::text::BidiClass::L) {
        return wsc::text::BidiClass::L;
    }
    if (bidiClass == wsc::text::BidiClass::R
        || bidiClass == wsc::text::BidiClass::AN
        || bidiClass == wsc::text::BidiClass::EN) {
        return wsc::text::BidiClass::R;
    }
    return wsc::text::BidiClass::ON;
}

bool bidiBracketsMatch(std::uint32_t expectedClosing, std::uint32_t actualClosing)
{
    if (expectedClosing == actualClosing) {
        return true;
    }
    return (expectedClosing == 0x232A && actualClosing == 0x3009)
        || (expectedClosing == 0x3009 && actualClosing == 0x232A);
}

void setBracketPairClass(std::vector<BidiItem> &items, std::size_t index, wsc::text::BidiClass bidiClass)
{
    items[index].resolvedClass = bidiClass;
    for (std::size_t cursor = index + 1; cursor < items.size(); ++cursor) {
        if (items[cursor].originalClass == wsc::text::BidiClass::NSM) {
            items[cursor].resolvedClass = bidiClass;
            continue;
        }
        if (items[cursor].originalClass == wsc::text::BidiClass::BN) {
            continue;
        }
        break;
    }
}

void resolvePairedBrackets(std::vector<BidiItem> &items, wsc::text::BidiClass startClass)
{
    struct BracketStackEntry
    {
        std::size_t index = 0;
        std::uint32_t closingCodepoint = 0;
    };
    struct BracketPair
    {
        std::size_t openingIndex = 0;
        std::size_t closingIndex = 0;
    };

    std::vector<BracketStackEntry> stack;
    std::vector<BracketPair> pairs;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].resolvedClass != wsc::text::BidiClass::ON) {
            continue;
        }
        const BidiBracketEntry *entry = lookupBidiBracket(items[i].codepoint.value);
        if (entry == nullptr) {
            continue;
        }
        if (entry->kind == 'o') {
            if (stack.size() < 63) {
                stack.push_back({i, entry->pair});
            }
            continue;
        }
        if (entry->kind != 'c') {
            continue;
        }
        for (std::size_t cursor = stack.size(); cursor > 0; --cursor) {
            if (bidiBracketsMatch(stack[cursor - 1].closingCodepoint, items[i].codepoint.value)) {
                pairs.push_back({stack[cursor - 1].index, i});
                stack.resize(cursor - 1);
                break;
            }
        }
    }

    for (const BracketPair &pair : pairs) {
        const wsc::text::BidiClass embeddingClass =
            (items[pair.openingIndex].level % 2) == 0 ? wsc::text::BidiClass::L : wsc::text::BidiClass::R;
        const wsc::text::BidiClass oppositeClass =
            embeddingClass == wsc::text::BidiClass::L ? wsc::text::BidiClass::R : wsc::text::BidiClass::L;

        bool containsEmbeddingStrong = false;
        bool containsOppositeStrong = false;
        for (std::size_t i = pair.openingIndex + 1; i < pair.closingIndex; ++i) {
            const wsc::text::BidiClass strong = bracketStrongClass(items[i].resolvedClass);
            containsEmbeddingStrong = containsEmbeddingStrong || strong == embeddingClass;
            containsOppositeStrong = containsOppositeStrong || strong == oppositeClass;
        }
        if (containsEmbeddingStrong) {
            setBracketPairClass(items, pair.openingIndex, embeddingClass);
            setBracketPairClass(items, pair.closingIndex, embeddingClass);
            continue;
        }
        if (!containsOppositeStrong) {
            continue;
        }

        wsc::text::BidiClass priorStrong = startClass;
        for (std::size_t cursor = pair.openingIndex; cursor > 0; --cursor) {
            const wsc::text::BidiClass strong = bracketStrongClass(items[cursor - 1].resolvedClass);
            if (strong == wsc::text::BidiClass::L || strong == wsc::text::BidiClass::R) {
                priorStrong = strong;
                break;
            }
        }
        const wsc::text::BidiClass resolved =
            priorStrong == oppositeClass ? oppositeClass : embeddingClass;
        setBracketPairClass(items, pair.openingIndex, resolved);
        setBracketPairClass(items, pair.closingIndex, resolved);
    }
}

void resolveWeakTypes(std::vector<BidiItem> &items, wsc::text::BidiClass startClass)
{
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].resolvedClass == wsc::text::BidiClass::NSM) {
            items[i].resolvedClass = i == 0 ? startClass : items[i - 1].resolvedClass;
        }
    }

    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].resolvedClass == wsc::text::BidiClass::EN
            && previousStrongForNumbers(items, i, startClass) == wsc::text::BidiClass::AL) {
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
            && previousStrongClass(items, i, startClass) == wsc::text::BidiClass::L) {
            items[i].resolvedClass = wsc::text::BidiClass::L;
        }
    }
}

void resolveNeutralTypes(std::vector<BidiItem> &items, wsc::text::BidiClass startClass,
                         wsc::text::BidiClass endClass)
{
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].preserveLevel) {
            continue;
        }
        if (!isNeutralClass(items[i].resolvedClass)) {
            continue;
        }

        const std::size_t start = i;
        std::size_t end = i;
        while (end + 1 < items.size()
               && !items[end + 1].preserveLevel
               && isNeutralClass(items[end + 1].resolvedClass)) {
            ++end;
        }

        const wsc::text::BidiClass before = previousNeutralBoundaryClass(items, start, startClass);
        const wsc::text::BidiClass after = nextNeutralBoundaryClass(items, end, endClass);
        const wsc::text::BidiClass resolved = before == after
            ? before
            : (items[start].level % 2 == 0 ? wsc::text::BidiClass::L : wsc::text::BidiClass::R);

        for (std::size_t cursor = start; cursor <= end; ++cursor) {
            if (!items[cursor].preserveLevel) {
                items[cursor].resolvedClass = resolved;
            }
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
        if (item.preserveLevel) {
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
            || item.originalClass == wsc::text::BidiClass::BN
            || item.originalClass == wsc::text::BidiClass::LRI
            || item.originalClass == wsc::text::BidiClass::RLI
            || item.originalClass == wsc::text::BidiClass::FSI
            || item.originalClass == wsc::text::BidiClass::PDI) {
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
                || item.originalClass == wsc::text::BidiClass::BN
                || item.originalClass == wsc::text::BidiClass::LRI
                || item.originalClass == wsc::text::BidiClass::RLI
                || item.originalClass == wsc::text::BidiClass::FSI
                || item.originalClass == wsc::text::BidiClass::PDI) {
                item.level = paragraphLevelValue;
                continue;
            }
            break;
        }
    }
}

wsc::text::BidiClass runBoundaryClass(int paragraphLevelValue, int adjacentLevel)
{
    return sorClass(paragraphLevelValue, adjacentLevel);
}

void resolveRuns(std::vector<BidiItem> &items, int paragraphLevelValue)
{
    std::vector<int> initialLevels;
    initialLevels.reserve(items.size());
    for (const BidiItem &item : items) {
        initialLevels.push_back(item.level);
    }

    std::size_t runStart = 0;
    while (runStart < items.size()) {
        const int runLevel = initialLevels[runStart];
        std::size_t runEnd = runStart;
        while (runEnd + 1 < items.size() && initialLevels[runEnd + 1] == runLevel) {
            ++runEnd;
        }

        const bool startsAfterClosedIsolate = items[runStart].originalClass == wsc::text::BidiClass::PDI
            && items[runStart].preserveLevel;
        const int previousLevel = runStart == 0 || startsAfterClosedIsolate
            ? paragraphLevelValue
            : initialLevels[runStart - 1];
        const int nextLevel = runEnd + 1 < items.size() ? initialLevels[runEnd + 1] : paragraphLevelValue;
        const wsc::text::BidiClass startClass = runBoundaryClass(paragraphLevelValue,
                                                                 std::max(previousLevel, runLevel));
        wsc::text::BidiClass endClass = runBoundaryClass(paragraphLevelValue,
                                                         std::max(nextLevel, runLevel));
        if (runLevel == paragraphLevelValue
            && items[runEnd].preserveLevel
            && isIsolateInitiator(items[runEnd].originalClass)) {
            endClass = paragraphLevelValue % 2 == 0 ? wsc::text::BidiClass::L : wsc::text::BidiClass::R;
        }

        std::vector<BidiItem> run(items.begin() + static_cast<std::ptrdiff_t>(runStart),
                                  items.begin() + static_cast<std::ptrdiff_t>(runEnd + 1));
        resolveWeakTypes(run, startClass);
        resolvePairedBrackets(run, startClass);
        resolveNeutralTypes(run, startClass, endClass);
        resolveImplicitLevels(run);
        for (std::size_t i = 0; i < run.size(); ++i) {
            items[runStart + i] = run[i];
        }

        runStart = runEnd + 1;
    }
}

std::vector<BidiItem> resolveItemsFromClasses(const std::vector<wsc::text::BidiClass> &classes,
                                              const std::vector<wsc::text::Utf8Codepoint> &codepoints,
                                              wsc::text::BidiParagraphDirection direction)
{
    const int baseLevel = paragraphLevel(classes, direction);
    std::vector<DirectionState> stack = {{baseLevel, std::nullopt, false}};
    std::vector<BidiItem> allItems;
    int overflowIsolateCount = 0;
    int overflowEmbeddingCount = 0;
    int validIsolateCount = 0;

    for (std::size_t index = 0; index < classes.size(); ++index) {
        const wsc::text::BidiClass bidiClass = classes[index];

        DirectionState current = stack.back();
        int itemLevel = current.level;
        std::optional<wsc::text::BidiClass> itemOverride = current.overrideClass;
        bool preserveLevel = false;
        auto pushEmbedding = [&](int level, std::optional<wsc::text::BidiClass> overrideClass) {
            if (level <= 125 && overflowIsolateCount == 0 && overflowEmbeddingCount == 0) {
                stack.push_back({level, overrideClass, false});
            } else if (overflowIsolateCount == 0) {
                ++overflowEmbeddingCount;
            }
        };
        auto pushIsolate = [&](int level) {
            if (level <= 125 && overflowIsolateCount == 0 && overflowEmbeddingCount == 0) {
                ++validIsolateCount;
                stack.push_back({level, std::nullopt, true});
            } else {
                ++overflowIsolateCount;
            }
        };

        if (bidiClass == wsc::text::BidiClass::LRE) {
            pushEmbedding(nextEvenLevel(current.level), std::nullopt);
        } else if (bidiClass == wsc::text::BidiClass::RLE) {
            pushEmbedding(nextOddLevel(current.level), std::nullopt);
        } else if (bidiClass == wsc::text::BidiClass::LRO) {
            pushEmbedding(nextEvenLevel(current.level), wsc::text::BidiClass::L);
        } else if (bidiClass == wsc::text::BidiClass::RLO) {
            pushEmbedding(nextOddLevel(current.level), wsc::text::BidiClass::R);
        } else if (bidiClass == wsc::text::BidiClass::LRI
                   || bidiClass == wsc::text::BidiClass::RLI
                   || bidiClass == wsc::text::BidiClass::FSI) {
            preserveLevel = true;
            if (current.overrideClass.has_value()) {
                itemOverride = current.overrideClass;
            } else {
                itemOverride = std::nullopt;
            }
            bool rtl = bidiClass == wsc::text::BidiClass::RLI;
            if (bidiClass == wsc::text::BidiClass::FSI) {
                rtl = firstStrongDirectionInIsolate(classes, index).value_or(false);
            }
            pushIsolate(rtl ? nextOddLevel(current.level) : nextEvenLevel(current.level));
        } else if (bidiClass == wsc::text::BidiClass::PDF) {
            if (overflowIsolateCount > 0) {
                // A PDF inside an overflow isolate has no effect.
            } else if (overflowEmbeddingCount > 0) {
                --overflowEmbeddingCount;
            } else if (stack.size() >= 2 && !stack.back().isolate) {
                stack.pop_back();
            }
        } else if (bidiClass == wsc::text::BidiClass::PDI) {
            bool closedValidIsolate = false;
            if (overflowIsolateCount > 0) {
                --overflowIsolateCount;
            } else if (validIsolateCount > 0) {
                overflowEmbeddingCount = 0;
                while (stack.size() > 1 && !stack.back().isolate) {
                    stack.pop_back();
                }
                if (stack.size() > 1 && stack.back().isolate) {
                    stack.pop_back();
                }
                --validIsolateCount;
                closedValidIsolate = true;
            }
            if (closedValidIsolate) {
                itemLevel = stack.back().level;
                itemOverride = stack.back().overrideClass;
                preserveLevel = true;
            }
        } else {
            current = stack.back();
            itemLevel = current.level;
            itemOverride = current.overrideClass;
        }
        if (bidiClass == wsc::text::BidiClass::B) {
            itemLevel = baseLevel;
            itemOverride = std::nullopt;
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
        item.preserveLevel = preserveLevel;
        allItems.push_back(item);
        if (bidiClass == wsc::text::BidiClass::B) {
            break;
        }
    }

    std::vector<std::size_t> isolateStack;
    for (std::size_t index = 0; index < allItems.size(); ++index) {
        if (isIsolateInitiator(allItems[index].originalClass)) {
            isolateStack.push_back(index);
            continue;
        }
        if (allItems[index].originalClass != wsc::text::BidiClass::PDI
            || !allItems[index].preserveLevel
            || isolateStack.empty()) {
            continue;
        }
        const std::size_t initiatorIndex = isolateStack.back();
        isolateStack.pop_back();

        bool hasVisibleContent = false;
        for (std::size_t cursor = initiatorIndex + 1; cursor < index; ++cursor) {
            if (allItems[cursor].visible && !isDirectionalIsolate(allItems[cursor].originalClass)) {
                hasVisibleContent = true;
                break;
            }
        }
        if (!hasVisibleContent) {
            allItems[initiatorIndex].preserveLevel = false;
            allItems[index].preserveLevel = false;
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
        resolveRuns(visibleItems, baseLevel);
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

    struct LeveledRun
    {
        BidiRun run;
        int level;
    };

    std::vector<LeveledRun> logicalRuns;
    std::size_t runStart = 0;
    for (std::size_t i = 1; i <= items.size(); ++i) {
        if (i < items.size() && items[i].level == items[runStart].level) {
            continue;
        }
        logicalRuns.push_back({{items[runStart].codepoint.offset,
                                items[i - 1].codepoint.offset + items[i - 1].codepoint.length,
                                items[runStart].level % 2 == 1},
                               items[runStart].level});
        runStart = i;
    }

    int maxLevel = 0;
    int minOddLevel = 126;
    for (const LeveledRun &run : logicalRuns) {
        maxLevel = std::max(maxLevel, run.level);
        if ((run.level % 2) == 1) {
            minOddLevel = std::min(minOddLevel, run.level);
        }
    }
    if (minOddLevel != 126) {
        for (int level = maxLevel; level >= minOddLevel; --level) {
            std::size_t start = 0;
            while (start < logicalRuns.size()) {
                while (start < logicalRuns.size() && logicalRuns[start].level < level) {
                    ++start;
                }
                std::size_t end = start;
                while (end < logicalRuns.size() && logicalRuns[end].level >= level) {
                    ++end;
                }
                std::reverse(logicalRuns.begin() + static_cast<std::ptrdiff_t>(start),
                             logicalRuns.begin() + static_cast<std::ptrdiff_t>(end));
                start = end;
            }
        }
    }

    std::vector<BidiRun> visualRuns;
    visualRuns.reserve(logicalRuns.size());
    for (const LeveledRun &run : logicalRuns) {
        visualRuns.push_back(run.run);
    }
    return visualRuns;
}

} // namespace wsc::text
