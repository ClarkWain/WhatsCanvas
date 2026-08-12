#pragma once

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wsc {

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

/// Identifies a desired font by family name, weight and slant.
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

/// A concrete font face loaded from a file or memory, registered on a Canvas.
class FontFace
{
public:
    /// Load a face from a font file on disk (optionally a specific face index).
    static FontFace fromFile(FontDescriptor descriptor, std::string path, int faceIndex = 0)
    {
        FontFace face;
        face.descriptor_ = std::move(descriptor);
        face.sourceType_ = FontSourceType::FILE;
        face.path_ = std::move(path);
        face.faceIndex_ = faceIndex < 0 ? 0 : faceIndex;
        return face;
    }

    static FontFace fromMemory(FontDescriptor descriptor, std::vector<std::uint8_t> bytes, int faceIndex = 0)
    {
        FontFace face;
        face.descriptor_ = std::move(descriptor);
        face.sourceType_ = FontSourceType::MEMORY;
        face.bytes_ = std::make_shared<std::vector<std::uint8_t>>(std::move(bytes));
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
    std::shared_ptr<std::vector<std::uint8_t>> bytes_;
    std::vector<FontCodepointRange> codepointRanges_;
};

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

    void addFallbackFamily(std::string family)
    {
        if (family.empty() || family == primaryFamily_ || contains(family)) {
            return;
        }
        fallbackFamilies_.push_back(std::move(family));
    }

    bool contains(const std::string &family) const
    {
        return std::find(fallbackFamilies_.begin(), fallbackFamilies_.end(), family) != fallbackFamilies_.end();
    }

    void clearFallbacks() { fallbackFamilies_.clear(); }
    const std::vector<std::string> &fallbackFamilies() const { return fallbackFamilies_; }

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

class FontManager
{
public:
    bool registerFontFile(const FontDescriptor &descriptor, const std::string &path, int faceIndex = 0)
    {
        return registerFace(FontFace::fromFile(descriptor, path, faceIndex));
    }

    bool registerFontMemory(const FontDescriptor &descriptor, std::vector<std::uint8_t> bytes, int faceIndex = 0)
    {
        return registerFace(FontFace::fromMemory(descriptor, std::move(bytes), faceIndex));
    }

    bool registerFace(FontFace face)
    {
        if (!face.isValid()) {
            return false;
        }

        const std::size_t index = faces_.size();
        familyToFaceIndices_[face.family()].push_back(index);
        fallbackChains_.try_emplace(face.family(), FontFallbackChain(face.family()));
        faces_.push_back(std::move(face));
        return true;
    }

    bool hasFamily(const std::string &family) const
    {
        const auto it = familyToFaceIndices_.find(family);
        return it != familyToFaceIndices_.end() && !it->second.empty();
    }

    const FontFace *findFirstFace(const std::string &family) const
    {
        const auto it = familyToFaceIndices_.find(family);
        if (it == familyToFaceIndices_.end() || it->second.empty()) {
            return nullptr;
        }
        return &faces_[it->second.front()];
    }

    std::vector<const FontFace *> findFaces(const std::string &family) const
    {
        std::vector<const FontFace *> result;
        const auto it = familyToFaceIndices_.find(family);
        if (it == familyToFaceIndices_.end()) {
            return result;
        }

        result.reserve(it->second.size());
        for (std::size_t index : it->second) {
            result.push_back(&faces_[index]);
        }
        return result;
    }

    const FontFace *findBestFace(const std::string &family, int weight = 400,
                                 FontSlant slant = FontSlant::NORMAL) const
    {
        const auto faces = findFaces(family);
        const FontFace *bestFace = nullptr;
        int bestScore = 0;
        const int requestedWeight = std::clamp(weight, 1, 1000);
        for (const FontFace *face : faces) {
            if (face == nullptr) {
                continue;
            }

            const int slantPenalty = face->slant() == slant ? 0 : 1000;
            const int weightPenalty = std::abs(face->weight() - requestedWeight);
            const int score = slantPenalty + weightPenalty;
            if (bestFace == nullptr || score < bestScore) {
                bestFace = face;
                bestScore = score;
            }
        }
        return bestFace;
    }

    bool addFallbackFamily(const std::string &primaryFamily, const std::string &fallbackFamily)
    {
        if (!hasFamily(primaryFamily) || !hasFamily(fallbackFamily)) {
            return false;
        }

        auto &chain = fallbackChains_[primaryFamily];
        chain.setPrimaryFamily(primaryFamily);
        chain.addFallbackFamily(fallbackFamily);
        return true;
    }

    const FontFallbackChain *fallbackChain(const std::string &family) const
    {
        const auto it = fallbackChains_.find(family);
        return it == fallbackChains_.end() ? nullptr : &it->second;
    }

    std::vector<std::string> resolveFamilies(const std::string &preferredFamily) const
    {
        const auto *chain = fallbackChain(preferredFamily);
        if (chain != nullptr) {
            return chain->familiesInResolutionOrder();
        }
        return preferredFamily.empty() ? std::vector<std::string>() : std::vector<std::string>{preferredFamily};
    }

    const std::vector<FontFace> &faces() const { return faces_; }
    void clear()
    {
        faces_.clear();
        familyToFaceIndices_.clear();
        fallbackChains_.clear();
    }

private:
    std::vector<FontFace> faces_;
    std::unordered_map<std::string, std::vector<std::size_t>> familyToFaceIndices_;
    std::unordered_map<std::string, FontFallbackChain> fallbackChains_;
};

class FontSystem
{
public:
    static constexpr const char *kDefaultPrimaryFamily = "WhatsCanvas Sans";
    static constexpr const char *kDefaultCjkFamily = "WhatsCanvas CJK";
    static constexpr const char *kDefaultArabicFamily = "WhatsCanvas Arabic";
    static constexpr const char *kDefaultHebrewFamily = "WhatsCanvas Hebrew";
    static constexpr const char *kDefaultSymbolFamily = "WhatsCanvas Symbol";
    static constexpr const char *kDefaultSerifFamily = "WhatsCanvas Serif";
    static constexpr const char *kDefaultMonoFamily = "WhatsCanvas Mono";

    static bool fileExists(const std::string &path)
    {
        std::ifstream stream(std::filesystem::u8path(path), std::ios::binary);
        return stream.good();
    }

    /// Build the portable fallback aliases from fonts reported by the native
    /// platform font manager. No operating-system font paths are assumed.
    static std::vector<FontFace> defaultSystemFontFaces();

    /// Discard both the discovery and the default-slot process-wide caches
    /// so the next discoverInstalledFontFaces() / defaultSystemFontFaces()
    /// call re-runs platform enumeration. Cheap to call; useful after the
    /// host installs a new font at runtime.
    static void refreshDefaultSystemFontFaces();

    /// Discard only the discoverInstalledFontFaces() cache. defaultSystemFontFaces()
    /// keeps its own cached slot table until refreshDefaultSystemFontFaces()
    /// is called. Prefer this entry point when the caller only consumes the
    /// discovery output directly (e.g. a font picker) and wants to avoid
    /// rebuilding the WhatsCanvas fallback slot table.
    static void refreshDiscoveredFontFaces();

    /// Discard only the defaultSystemFontFaces() cache. The next call
    /// rebuilds the slot table from the cached discovery output; call
    /// refreshDiscoveredFontFaces() first if the underlying installed
    /// font set has actually changed.
    static void refreshDefaultSystemFontFacesOnly();

    static FontFallbackChain defaultFallbackChain(const std::string &primaryFamily = kDefaultPrimaryFamily)
    {
        FontFallbackChain chain(primaryFamily);
        chain.addFallbackFamily(kDefaultCjkFamily);
        chain.addFallbackFamily(kDefaultArabicFamily);
        chain.addFallbackFamily(kDefaultHebrewFamily);
        chain.addFallbackFamily(kDefaultSymbolFamily);
        chain.addFallbackFamily(kDefaultSerifFamily);
        chain.addFallbackFamily(kDefaultMonoFamily);
        return chain;
    }

    /// Query the platform's native font manager (CoreText on macOS,
    /// DirectWrite on Windows, fontconfig on Linux) for every installed
    /// font face. Each returned FontFace is registered under its real
    /// system family name (e.g. "Menlo", "Consolas", "PingFang SC"),
    /// weight, and file path. Returns an empty vector when the platform
    /// API is unavailable or not linked in.
    static std::vector<FontFace> discoverInstalledFontFaces();
};

} // namespace wsc
