#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Export.h"

namespace wsc {

/// Return the stable lookup key used for font-family matching. Display names
/// remain unchanged on FontFace; only ASCII case and whitespace are
/// canonicalized so UTF-8 platform family names are preserved byte-for-byte.
inline std::string canonicalFontFamilyName(const std::string &family)
{
    std::string result;
    result.reserve(family.size());

    bool pendingSpace = false;
    for (unsigned char ch : family) {
        const bool asciiWhitespace = ch == ' ' || ch == '\t' || ch == '\n'
            || ch == '\r' || ch == '\f' || ch == '\v';
        if (asciiWhitespace) {
            pendingSpace = !result.empty();
            continue;
        }
        if (pendingSpace) {
            result.push_back(' ');
            pendingSpace = false;
        }
        result.push_back(ch >= 'A' && ch <= 'Z'
                             ? static_cast<char>(ch - 'A' + 'a')
                             : static_cast<char>(ch));
    }
    return result;
}

/// Slant style of a font face.
enum class FontSlant
{
    NORMAL,
    ITALIC,
    OBLIQUE
};

/// Whether a font face is backed by a file path or an in-memory buffer.
enum class FontSourceType
{
    FILE,
    MEMORY
};

/// Identifies a desired font by UTF-8 family name, weight and slant. Weight
/// follows the CSS/OpenType 1-1000 scale (400 normal, 700 bold).
struct FontDescriptor
{
    std::string family;
    int weight = 400;
    FontSlant slant = FontSlant::NORMAL;

    FontDescriptor() = default;

    explicit FontDescriptor(std::string familyName)
        : family(std::move(familyName))
    {
    }

    FontDescriptor(std::string familyName, int fontWeight, FontSlant fontSlant = FontSlant::NORMAL)
        : family(std::move(familyName)),
          weight(fontWeight),
          slant(fontSlant)
    {
    }
};

/// An inclusive range of Unicode code points, used to scope a font face.
struct FontCodepointRange
{
    std::uint32_t first = 0;
    std::uint32_t last = 0;

    FontCodepointRange() = default;

    FontCodepointRange(std::uint32_t firstCodepoint, std::uint32_t lastCodepoint)
        : first(firstCodepoint),
          last(lastCodepoint)
    {
    }

    bool contains(std::uint32_t codepoint) const
    {
        return first <= last && codepoint >= first && codepoint <= last;
    }
};

/// A user-space coordinate for one OpenType variable-font axis. The tag must
/// contain exactly four bytes (for example, "wght" or "wdth").
struct FontVariationCoordinate
{
    std::string tag;
    float value = 0.0f;
};

/// Copyable description of one concrete font face registered on a Canvas.
///
/// Factory functions do not parse the font immediately. Canvas registration or
/// provider matching validates/opens the selected face. Memory factories retain
/// immutable bytes; file-backed faces require the path to remain readable when
/// the Canvas first materializes the face.
class FontFace
{
public:
    /// Describe a face from a font file (optionally a TTC/OTC face index).
    static FontFace fromFile(FontDescriptor descriptor, std::string path, int faceIndex = 0)
    {
        FontFace face;
        face.descriptor_ = std::move(descriptor);
        face.sourceType_ = FontSourceType::FILE;
        face.path_ = std::move(path);
        face.faceIndex_ = faceIndex < 0 ? 0 : faceIndex;
        return face;
    }

    /// Take ownership of an immutable font-file byte snapshot.
    static FontFace fromMemory(FontDescriptor descriptor, std::vector<std::uint8_t> bytes, int faceIndex = 0)
    {
        return fromSharedMemory(
            std::move(descriptor),
            std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes)),
            faceIndex);
    }

    /// Retain an immutable byte snapshot supplied by a platform/provider.
    /// sourceId is diagnostic identity only; rendering never reopens it. This
    /// lets a provider materialize an Android system font while its AFont
    /// handle/path is valid and keep using it after that platform handle closes.
    static FontFace fromSharedMemory(
        FontDescriptor descriptor,
        std::shared_ptr<const std::vector<std::uint8_t>> bytes,
        int faceIndex = 0, std::string sourceId = std::string())
    {
        FontFace face;
        face.descriptor_ = std::move(descriptor);
        face.sourceType_ = FontSourceType::MEMORY;
        face.bytes_ = std::move(bytes);
        face.sourceId_ = std::move(sourceId);
        face.faceIndex_ = faceIndex < 0 ? 0 : faceIndex;
        return face;
    }

    const FontDescriptor &descriptor() const { return descriptor_; }

    const std::string &family() const { return descriptor_.family; }

    int weight() const { return descriptor_.weight; }

    FontSlant slant() const { return descriptor_.slant; }

    FontSourceType sourceType() const { return sourceType_; }

    int faceIndex() const { return faceIndex_; }

    const std::string &path() const { return path_; }

    const std::vector<std::uint8_t> *bytes() const { return bytes_ ? bytes_.get() : nullptr; }

    const std::shared_ptr<const std::vector<std::uint8_t>> &sharedBytes() const
    {
        return bytes_;
    }

    /// Provider-owned logical origin for memory-backed data. It may be a
    /// platform identifier and is not required to be an openable file path.
    const std::string &sourceId() const { return sourceId_; }

    /// Set one four-byte OpenType variation axis; returns false for an invalid
    /// tag or non-finite value.
    bool setVariationCoordinate(std::string tag, float value)
    {
        if (tag.size() != 4 || !std::isfinite(value)) return false;
        const auto found = std::find_if(
            variationCoordinates_.begin(), variationCoordinates_.end(),
            [&](const FontVariationCoordinate &coordinate) {
                return coordinate.tag == tag;
            });
        if (found != variationCoordinates_.end()) {
            found->value = value;
        } else {
            variationCoordinates_.push_back({std::move(tag), value});
        }
        return true;
    }

    const std::vector<FontVariationCoordinate> &variationCoordinates() const
    {
        return variationCoordinates_;
    }

    /// Add provider-supplied inclusive coverage metadata. This is a resolution
    /// hint, not a replacement for inspecting the font cmap.
    void addCodepointRange(std::uint32_t firstCodepoint, std::uint32_t lastCodepoint)
    {
        if (firstCodepoint <= lastCodepoint) {
            codepointRanges_.emplace_back(firstCodepoint, lastCodepoint);
        }
    }

    const std::vector<FontCodepointRange> &codepointRanges() const { return codepointRanges_; }

    bool hasCodepointRanges() const { return !codepointRanges_.empty(); }

    bool supportsCodepoint(std::uint32_t codepoint) const
    {
        return std::any_of(codepointRanges_.begin(), codepointRanges_.end(),
                           [codepoint](const FontCodepointRange &range) {
                               return range.contains(codepoint);
                           });
    }

    /// Structural descriptor validity only; it does not guarantee decodable font data.
    bool isValid() const
    {
        return !descriptor_.family.empty()
            && ((sourceType_ == FontSourceType::FILE && !path_.empty())
                || (sourceType_ == FontSourceType::MEMORY && bytes_ && !bytes_->empty()));
    }

private:
    FontDescriptor descriptor_;
    FontSourceType sourceType_ = FontSourceType::FILE;
    int faceIndex_ = 0;
    std::string path_;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
    std::string sourceId_;
    std::vector<FontCodepointRange> codepointRanges_;
    std::vector<FontVariationCoordinate> variationCoordinates_;
};

/// Ordered family fallback policy for one primary family. Family comparisons
/// use canonical ASCII case/whitespace matching while display spelling is kept.
class FontFallbackChain
{
public:
    FontFallbackChain() = default;

    explicit FontFallbackChain(std::string primaryFamily)
        : primaryFamily_(std::move(primaryFamily))
    {
    }

    const std::string &primaryFamily() const { return primaryFamily_; }

    void setPrimaryFamily(std::string family) { primaryFamily_ = std::move(family); }

    /// Append one non-empty family unless its canonical name is already present.
    void addFallbackFamily(std::string family)
    {
        if (family.empty()
            || canonicalFontFamilyName(family) == canonicalFontFamilyName(primaryFamily_)
            || contains(family)) {
            return;
        }
        fallbackFamilies_.push_back(std::move(family));
    }

    bool contains(const std::string &family) const
    {
        const std::string key = canonicalFontFamilyName(family);
        return std::any_of(fallbackFamilies_.begin(), fallbackFamilies_.end(),
                           [&](const std::string &candidate) {
                               return canonicalFontFamilyName(candidate) == key;
                           });
    }

    void clearFallbacks() { fallbackFamilies_.clear(); }

    const std::vector<std::string> &fallbackFamilies() const { return fallbackFamilies_; }

    /// Return primary first followed by explicit fallbacks.
    std::vector<std::string> familiesInResolutionOrder() const
    {
        std::vector<std::string> families;
        if (!primaryFamily_.empty()) {
            families.push_back(primaryFamily_);
        }
        families.insert(families.end(), fallbackFamilies_.begin(), fallbackFamilies_.end());
        return families;
    }

private:
    std::string primaryFamily_;
    std::vector<std::string> fallbackFamilies_;
};

} // namespace wsc
