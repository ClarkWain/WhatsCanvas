#pragma once

/**
 * @file FontSystem.h
 * @brief Opt-in font registration and installed-font discovery APIs.
 *
 * Basic text drawing only needs Font.h. Include this header when an
 * application owns a FontManager or directly controls the process-wide
 * installed-font cache.
 */

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Export.h"
#include "Font.h"

namespace wsc {

/// Owning collection of file/memory FontFace values with style matching.
///
/// Registration copies/owns all descriptors and bytes. Face pointers returned
/// by lookup remain stable across additional registrations but are invalidated
/// by clear(), assignment or destruction. This container is not thread-safe.
class FontManager
{
public:
    FontManager() = default;

    FontManager(const FontManager &other)
    {
        copyFrom(other);
    }

    FontManager &operator=(const FontManager &other)
    {
        if (this != &other) copyFrom(other);
        return *this;
    }

    FontManager(FontManager &&) noexcept = default;

    FontManager &operator=(FontManager &&) noexcept = default;

    /// Register a structurally valid descriptor. File readability/font parsing
    /// is deferred to the rendering backend.
    bool registerFontFile(const FontDescriptor &descriptor, const std::string &path, int faceIndex = 0)
    {
        return registerFace(FontFace::fromFile(descriptor, path, faceIndex));
    }

    bool registerFontMemory(const FontDescriptor &descriptor, std::vector<std::uint8_t> bytes, int faceIndex = 0)
    {
        return registerFace(FontFace::fromMemory(descriptor, std::move(bytes), faceIndex));
    }

    /// Store a structurally valid face; returns false without mutation otherwise.
    bool registerFace(FontFace face)
    {
        if (!face.isValid()) {
            return false;
        }

        const std::size_t index = faces_.size();
        const std::string familyKey = canonicalFontFamilyName(face.family());
        familyToFaceIndices_[familyKey].push_back(index);
        fallbackChains_.try_emplace(familyKey, FontFallbackChain(face.family()));
        auto stableFace = std::make_unique<FontFace>(std::move(face));
        faces_.push_back(*stableFace);
        faceStorage_.push_back(std::move(stableFace));
        advanceGeneration();
        return true;
    }

    bool hasFamily(const std::string &family) const
    {
        const auto it = familyToFaceIndices_.find(canonicalFontFamilyName(family));
        return it != familyToFaceIndices_.end() && !it->second.empty();
    }

    /// Return a borrowed stable face pointer, or nullptr when the family is unknown.
    const FontFace *findFirstFace(const std::string &family) const
    {
        const auto it = familyToFaceIndices_.find(canonicalFontFamilyName(family));
        if (it == familyToFaceIndices_.end() || it->second.empty()) {
            return nullptr;
        }
        return faceStorage_[it->second.front()].get();
    }

    std::vector<const FontFace *> findFaces(const std::string &family) const
    {
        std::vector<const FontFace *> result;
        const auto it = familyToFaceIndices_.find(canonicalFontFamilyName(family));
        if (it == familyToFaceIndices_.end()) {
            return result;
        }

        result.reserve(it->second.size());
        for (std::size_t index : it->second) {
            result.push_back(faceStorage_[index].get());
        }
        return result;
    }

    const FontFace *findBestFace(const std::string &family, int weight = 400,
                                 FontSlant slant = FontSlant::NORMAL) const
    {
        const auto faces = findFacesInMatchOrder(family, weight, slant);
        return faces.empty() ? nullptr : faces.front();
    }

    /// Return every face in CSS-compatible style preference order. This keeps
    /// coverage checks separate from style selection: a resolver may reject
    /// the first face when it does not cover the requested character cluster.
    std::vector<const FontFace *> findFacesInMatchOrder(
        const std::string &family, int weight = 400,
        FontSlant slant = FontSlant::NORMAL) const
    {
        auto result = findFaces(family);
        const int requestedWeight = std::clamp(weight, 1, 1000);
        std::stable_sort(result.begin(), result.end(),
                         [&](const FontFace *left, const FontFace *right) {
            if (left == nullptr) return false;
            if (right == nullptr) return true;
            return styleMatchRank(*left, requestedWeight, slant)
                < styleMatchRank(*right, requestedWeight, slant);
        });
        return result;
    }

    /// Append a fallback only when both families are already registered.
    bool addFallbackFamily(const std::string &primaryFamily, const std::string &fallbackFamily)
    {
        if (!hasFamily(primaryFamily) || !hasFamily(fallbackFamily)) {
            return false;
        }

        const std::string primaryKey = canonicalFontFamilyName(primaryFamily);
        auto &chain = fallbackChains_[primaryKey];
        const FontFace *primaryFace = findFirstFace(primaryFamily);
        const FontFace *fallbackFace = findFirstFace(fallbackFamily);
        chain.setPrimaryFamily(primaryFace == nullptr ? primaryFamily : primaryFace->family());
        chain.addFallbackFamily(fallbackFace == nullptr ? fallbackFamily : fallbackFace->family());
        advanceGeneration();
        return true;
    }

    const FontFallbackChain *fallbackChain(const std::string &family) const
    {
        const auto it = fallbackChains_.find(canonicalFontFamilyName(family));
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

    std::uint64_t generation() const { return generation_; }

    void clear()
    {
        faces_.clear();
        faceStorage_.clear();
        familyToFaceIndices_.clear();
        fallbackChains_.clear();
        advanceGeneration();
    }

private:
    void copyFrom(const FontManager &other)
    {
        faces_ = other.faces_;
        familyToFaceIndices_ = other.familyToFaceIndices_;
        fallbackChains_ = other.fallbackChains_;
        generation_ = other.generation_;
        faceStorage_.clear();
        faceStorage_.reserve(faces_.size());
        for (const FontFace &face : faces_) {
            faceStorage_.push_back(std::make_unique<FontFace>(face));
        }
    }

    static int slantMatchRank(FontSlant actual, FontSlant requested)
    {
        if (actual == requested) return 0;
        if (actual != FontSlant::NORMAL && requested != FontSlant::NORMAL) return 1;
        return 2;
    }

    // CSS Fonts matching deliberately does not use absolute weight distance.
    // Around 400/500 the standard has a special preference order.
    static std::pair<int, int> weightMatchRank(int actual, int requested)
    {
        actual = std::clamp(actual, 1, 1000);
        if (requested >= 400 && requested <= 500) {
            if (actual >= requested && actual <= 500) return {0, actual - requested};
            if (actual < requested) return {1, requested - actual};
            return {2, actual - 500};
        }
        if (requested < 400) {
            if (actual <= requested) return {0, requested - actual};
            return {1, actual - requested};
        }
        if (actual >= requested) return {0, actual - requested};
        return {1, requested - actual};
    }

    static std::tuple<int, int, int> styleMatchRank(
        const FontFace &face, int requestedWeight, FontSlant requestedSlant)
    {
        const auto weightRank = weightMatchRank(face.weight(), requestedWeight);
        return {slantMatchRank(face.slant(), requestedSlant),
                weightRank.first, weightRank.second};
    }

    void advanceGeneration()
    {
        ++generation_;
        if (generation_ == 0) generation_ = 1;
    }

    std::vector<FontFace> faces_;
    // Public faces() retains its historical contiguous snapshot while lookup
    // pointers come from stable allocations, so registering another face does
    // not invalidate a previously returned resolution result.
    std::vector<std::unique_ptr<FontFace>> faceStorage_;
    std::unordered_map<std::string, std::vector<std::size_t>> familyToFaceIndices_;
    std::unordered_map<std::string, FontFallbackChain> fallbackChains_;
    std::uint64_t generation_ = 1;
};

/// Process-wide native installed-font discovery and portable fallback aliases.
/// Applications normally use Canvas::refreshSystemFonts() rather than calling
/// the cache controls directly.
class WSC_API FontSystem
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
    /// platform font manager's current cached snapshot. No operating-system
    /// font paths are assumed.
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

    /// Re-enumerate installed fonts, publish a new process-wide snapshot, and
    /// invalidate the default-slot cache. Returns the monotonically increasing
    /// discovery generation. This is the preferred high-level refresh API.
    static std::uint64_t refreshInstalledFonts();

    /// Generation of the current discovery snapshot, or zero before the first
    /// discovery/default lookup or explicit refresh is requested.
    static std::uint64_t installedFontGeneration();

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
