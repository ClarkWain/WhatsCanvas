#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Font.h"

namespace wsc {

/// Font source layers are queried in this order for the same family. Explicit
/// fallback families are considered only after all providers have had a chance
/// to satisfy the requested family.
enum class FontProviderKind
{
    DYNAMIC,
    ASSET,
    TEST,
    SYSTEM
};

struct FontMatchRequest
{
    std::string family;
    int weight = 400;
    FontSlant slant = FontSlant::NORMAL;
    std::string locale;
    std::vector<std::uint32_t> codepoints;
    bool allowFallback = true;
};

struct FontMatchResult
{
    /// Non-owning pointer. It remains valid until the winning provider mutates.
    /// Callers must invalidate cached results when FontResolver::generation()
    /// changes.
    const FontFace *face = nullptr;
    std::string requestedFamily;
    std::string resolvedFamily;
    std::string providerName;
    FontProviderKind providerKind = FontProviderKind::SYSTEM;
    bool usedFallback = false;
    bool coverageVerified = false;

    explicit operator bool() const { return face != nullptr; }
};

class FontProvider
{
public:
    virtual ~FontProvider() = default;
    virtual FontProviderKind kind() const = 0;
    virtual const std::string &name() const = 0;
    virtual std::uint64_t generation() const = 0;
    /// Family-scoped generation for resolution caches. Providers without
    /// targeted invalidation may return their global generation.
    virtual std::uint64_t generationForFamily(const std::string &) const
    {
        return generation();
    }
    /// Invalidate provider-owned discovery and resolution caches after the
    /// host reports a system-font change. Static asset providers may no-op.
    virtual void refresh() {}
    virtual bool hasFamily(const std::string &family) const = 0;
    /// Families known from provider metadata. Platform providers that only
    /// support open-ended character matching may return an empty list.
    virtual std::vector<std::string> families() const { return {}; }
    /// Return a stable display spelling without forcing a lazy source load.
    virtual std::string displayFamilyName(const std::string &family) const
    {
        return family;
    }

    /// Return candidates in provider-specific style preference order. Platform
    /// providers may use request.locale/codepoints; file providers normally
    /// defer definitive glyph coverage to FontResolver's coverage predicate.
    virtual std::vector<const FontFace *> match(const FontMatchRequest &request) const = 0;
};

/// Adapts the existing file/memory FontManager to the provider contract.
class FontManagerProvider final : public FontProvider
{
public:
    FontManagerProvider(std::shared_ptr<FontManager> manager, FontProviderKind providerKind,
                        std::string providerName)
        : manager_(std::move(manager)), kind_(providerKind), name_(std::move(providerName))
    {
    }

    FontProviderKind kind() const override { return kind_; }
    const std::string &name() const override { return name_; }
    std::uint64_t generation() const override
    {
        return manager_ ? manager_->generation() : 0;
    }
    bool hasFamily(const std::string &family) const override
    {
        return manager_ && manager_->hasFamily(family);
    }
    std::string displayFamilyName(const std::string &family) const override
    {
        if (!manager_) return family;
        const FontFace *face = manager_->findFirstFace(family);
        return face == nullptr ? family : face->family();
    }
    std::vector<std::string> families() const override
    {
        std::vector<std::string> result;
        if (!manager_) return result;
        for (const FontFace &face : manager_->faces()) {
            const std::string key = canonicalFontFamilyName(face.family());
            const bool seen = std::any_of(
                result.begin(), result.end(), [&](const std::string &family) {
                    return canonicalFontFamilyName(family) == key;
                });
            if (!seen) result.push_back(face.family());
        }
        return result;
    }
    std::vector<const FontFace *> match(const FontMatchRequest &request) const override
    {
        return manager_
            ? manager_->findFacesInMatchOrder(request.family, request.weight, request.slant)
            : std::vector<const FontFace *>{};
    }

private:
    std::shared_ptr<FontManager> manager_;
    FontProviderKind kind_;
    std::string name_;
};

/// Metadata for an application font whose bytes are supplied on first match.
/// sourceId is an application-owned stable key (asset path, bundle key, URL,
/// or content identifier); it is passed to LazyFontProvider::Loader.
struct LazyFontSource
{
    FontDescriptor descriptor;
    std::string sourceId;
    /// Optional host-provided content revision/hash. When non-empty, an exact
    /// metadata re-registration with the same fingerprint is a no-op; changing
    /// it invalidates loaded, failed, queued, or downloading state.
    std::string fingerprint;
    int faceIndex = 0;
    std::vector<FontCodepointRange> codepointRanges;
    std::vector<FontVariationCoordinate> variationCoordinates;
};

/// Provider for asset/dynamic/test fonts that defers byte loading until the
/// source family is first matched. Failed loads are memoized until the family
/// is explicitly invalidated or its source registration is replaced.
class WSC_API LazyFontProvider final : public FontProvider
{
public:
    using Loader = std::function<std::optional<std::vector<std::uint8_t>>(
        const std::string &sourceId)>;

    LazyFontProvider(FontProviderKind providerKind, std::string providerName,
                     Loader loader);
    ~LazyFontProvider() override;
    LazyFontProvider(const LazyFontProvider &) = delete;
    LazyFontProvider &operator=(const LazyFontProvider &) = delete;

    FontProviderKind kind() const override;
    const std::string &name() const override;
    std::uint64_t generation() const override;
    std::uint64_t generationForFamily(const std::string &family) const override;
    void refresh() override;
    bool hasFamily(const std::string &family) const override;
    std::vector<std::string> families() const override;
    std::string displayFamilyName(const std::string &family) const override;
    std::vector<const FontFace *> match(
        const FontMatchRequest &request) const override;

    /// Add or replace one source identified by (family, sourceId). Registration
    /// validates metadata but does not invoke Loader.
    bool registerSource(LazyFontSource source);
    /// Forget loaded/failed state for one family so its sources can be retried.
    bool invalidateFamily(const std::string &family);
    /// Remove every source in one family. Previously returned pointers become
    /// invalid and callers must observe the changed generation.
    bool removeFamily(const std::string &family);
    std::size_t sourceCount() const;
    std::size_t loadedFaceCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Lifecycle of a remotely supplied font source. The provider never performs
/// network I/O itself; a Web/native host drains queued requests and reports the
/// result back on its event loop.
enum class RemoteFontState
{
    UNKNOWN,
    IDLE,
    QUEUED,
    DOWNLOADING,
    LOADED,
    PERMANENT_FAILURE
};

struct RemoteFontSource
{
    LazyFontSource font;
    /// Estimated transfer size used to reserve the provider download budget.
    /// Zero means unknown and is checked against the budget on completion.
    std::size_t expectedBytes = 0;
    /// Lower values win after coverage and style preference are considered.
    int priority = 0;
};

struct RemoteFontRequest
{
    std::string sourceId;
    std::string family;
    std::size_t expectedBytes = 0;
    std::size_t attempt = 0;
    /// Unique token for this attempt. Completion callbacks must echo it so a
    /// stale response cannot satisfy a replaced or retried source.
    std::uint64_t requestToken = 0;
};

struct RemoteFontProviderOptions
{
    std::size_t maxConcurrentDownloads = 4;
    std::size_t maxAttemptsPerSource = 3;
    std::size_t maxCandidatesPerMatch = 5;
    /// Cumulative bytes transferred by successful and reported failed
    /// downloads. Zero disables the budget.
    std::size_t downloadBudgetBytes = 16u * 1024u * 1024u;
};

/// Host-driven asynchronous provider for Web fallback subsets, cloud fonts,
/// or any source that cannot synchronously return bytes from FontProvider::match.
class WSC_API RemoteFontProvider final : public FontProvider
{
public:
    RemoteFontProvider(FontProviderKind providerKind, std::string providerName,
                       RemoteFontProviderOptions options = {});
    ~RemoteFontProvider() override;
    RemoteFontProvider(const RemoteFontProvider &) = delete;
    RemoteFontProvider &operator=(const RemoteFontProvider &) = delete;

    FontProviderKind kind() const override;
    const std::string &name() const override;
    std::uint64_t generation() const override;
    std::uint64_t generationForFamily(const std::string &family) const override;
    void refresh() override;
    bool hasFamily(const std::string &family) const override;
    std::vector<std::string> families() const override;
    std::string displayFamilyName(const std::string &family) const override;
    std::vector<const FontFace *> match(
        const FontMatchRequest &request) const override;

    /// sourceId must be unique within this provider. Re-registering it replaces
    /// metadata and invalidates any loaded/failed instance.
    bool registerSource(RemoteFontSource source);
    bool removeFamily(const std::string &family);
    bool invalidateFamily(const std::string &family);

    /// Move queued sources to DOWNLOADING, respecting maxConcurrentDownloads.
    /// The host starts these requests and later calls completeDownload or
    /// failDownload on the same source id.
    std::vector<RemoteFontRequest> takeDownloadRequests();
    /// Drain family changes accumulated since the previous call. Results are
    /// deduplicated and sorted so the host can schedule one safe-frame
    /// relayout/repaint instead of re-entering layout from download callbacks.
    std::vector<std::string> takeChangedFamilies();
    bool completeDownload(const std::string &sourceId,
                          std::uint64_t requestToken,
                          std::vector<std::uint8_t> bytes);
    /// consumedBytes counts transfer budget already spent by the failed try.
    /// Non-permanent failures return to IDLE until maxAttemptsPerSource.
    bool failDownload(const std::string &sourceId,
                      std::uint64_t requestToken, bool permanent,
                      std::size_t consumedBytes = 0);

    RemoteFontState state(const std::string &sourceId) const;
    std::size_t queuedCount() const;
    std::size_t downloadingCount() const;
    std::size_t downloadedBytes() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class FontResolver
{
public:
    using CoveragePredicate = std::function<bool(
        const FontFace &, const std::vector<std::uint32_t> &)>;

    void addProvider(std::shared_ptr<FontProvider> provider)
    {
        if (!provider) return;
        providers_.push_back(std::move(provider));
        std::stable_sort(providers_.begin(), providers_.end(),
                         [](const auto &left, const auto &right) {
            return providerPriority(left->kind()) < providerPriority(right->kind());
        });
        advanceGeneration();
    }

    bool hasFamily(const std::string &family) const
    {
        return std::any_of(providers_.begin(), providers_.end(),
                           [&](const auto &provider) {
                               return provider->hasFamily(family);
                           });
    }

    void refreshProviders(FontProviderKind kind)
    {
        for (const auto &provider : providers_) {
            if (provider->kind() == kind) provider->refresh();
        }
        advanceGeneration();
    }

    /// Replace the fallback chain for its primary family. Known families are
    /// retained even if another entry is unavailable, and false reports that
    /// the caller supplied at least one unknown family.
    bool setFallbackChain(const FontFallbackChain &chain)
    {
        if (chain.primaryFamily().empty() || !hasFamily(chain.primaryFamily())) {
            return false;
        }
        FontFallbackChain accepted(displayFamily(chain.primaryFamily()));
        bool allKnown = true;
        for (const std::string &family : chain.fallbackFamilies()) {
            if (hasFamily(family)) {
                accepted.addFallbackFamily(displayFamily(family));
            } else {
                allKnown = false;
            }
        }
        fallbackChains_.insert_or_assign(
            canonicalFontFamilyName(chain.primaryFamily()), std::move(accepted));
        advanceGeneration();
        return allKnown;
    }

    std::vector<std::string> resolveFamilies(const std::string &preferredFamily) const
    {
        if (preferredFamily.empty()) return {};
        const auto found = fallbackChains_.find(canonicalFontFamilyName(preferredFamily));
        if (found != fallbackChains_.end()) {
            return found->second.familiesInResolutionOrder();
        }
        return {displayFamily(preferredFamily)};
    }

    FontMatchResult resolve(const FontMatchRequest &request,
                            const CoveragePredicate &covers = {}) const
    {
        FontMatchResult result;
        result.requestedFamily = request.family;
        if (request.family.empty()) return result;

        const std::vector<std::string> families = request.allowFallback
            ? resolveFamilies(request.family)
            : std::vector<std::string>{displayFamily(request.family)};
        for (std::size_t familyIndex = 0; familyIndex < families.size(); ++familyIndex) {
            FontMatchRequest providerRequest = request;
            providerRequest.family = families[familyIndex];
            providerRequest.allowFallback = false;
            for (const auto &provider : providers_) {
                if (!provider->hasFamily(providerRequest.family)) continue;
                for (const FontFace *face : provider->match(providerRequest)) {
                    if (face == nullptr) continue;
                    bool verified = request.codepoints.empty();
                    if (covers) {
                        verified = covers(*face, request.codepoints);
                        if (!verified) continue;
                    } else if (!request.codepoints.empty() && face->hasCodepointRanges()) {
                        verified = std::all_of(
                            request.codepoints.begin(), request.codepoints.end(),
                            [&](std::uint32_t codepoint) {
                                return face->supportsCodepoint(codepoint);
                            });
                        if (!verified) continue;
                    }
                    result.face = face;
                    result.resolvedFamily = face->family();
                    result.providerName = provider->name();
                    result.providerKind = provider->kind();
                    result.usedFallback = familyIndex != 0;
                    result.coverageVerified = verified;
                    return result;
                }
            }
        }
        return result;
    }

    /// Changes whenever provider membership, fallback policy, or a provider's
    /// own generation changes. Suitable for resolution/layout cache keys.
    std::uint64_t generation() const
    {
        std::uint64_t hash = generation_;
        for (const auto &provider : providers_) {
            hash ^= provider->generation() + 0x9e3779b97f4a7c15ULL
                + (hash << 6U) + (hash >> 2U);
        }
        return hash == 0 ? 1 : hash;
    }

    /// Generation scoped to the requested family and its explicit fallback
    /// chain. Lazy provider changes in unrelated families do not affect it.
    std::uint64_t resolutionGeneration(const std::string &preferredFamily) const
    {
        std::uint64_t hash = generation_;
        const std::vector<std::string> families = resolveFamilies(preferredFamily);
        for (const std::string &family : families) {
            for (const auto &provider : providers_) {
                hash ^= provider->generationForFamily(family)
                    + 0x9e3779b97f4a7c15ULL + (hash << 6U) + (hash >> 2U);
            }
        }
        return hash == 0 ? 1 : hash;
    }

private:
    static int providerPriority(FontProviderKind kind)
    {
        switch (kind) {
        case FontProviderKind::DYNAMIC: return 0;
        case FontProviderKind::ASSET: return 1;
        case FontProviderKind::TEST: return 2;
        case FontProviderKind::SYSTEM: return 3;
        }
        return 4;
    }

    std::string displayFamily(const std::string &family) const
    {
        for (const auto &provider : providers_) {
            if (!provider->hasFamily(family)) continue;
            return provider->displayFamilyName(family);
        }
        return family;
    }

    void advanceGeneration()
    {
        ++generation_;
        if (generation_ == 0) generation_ = 1;
    }

    std::vector<std::shared_ptr<FontProvider>> providers_;
    std::unordered_map<std::string, FontFallbackChain> fallbackChains_;
    std::uint64_t generation_ = 1;
};

} // namespace wsc
