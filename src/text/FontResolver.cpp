#include "wsc/FontResolver.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace {

int slantMatchRank(wsc::FontSlant actual, wsc::FontSlant requested)
{
    if (actual == requested) return 0;
    if (actual != wsc::FontSlant::NORMAL
        && requested != wsc::FontSlant::NORMAL) return 1;
    return 2;
}

std::pair<int, int> weightMatchRank(int actual, int requested)
{
    actual = std::clamp(actual, 1, 1000);
    requested = std::clamp(requested, 1, 1000);
    if (requested >= 400 && requested <= 500) {
        if (actual >= requested && actual <= 500) {
            return {0, actual - requested};
        }
        if (actual < requested) return {1, requested - actual};
        return {2, actual - 500};
    }
    if (requested < 400) {
        return actual <= requested
            ? std::pair<int, int>{0, requested - actual}
            : std::pair<int, int>{1, actual - requested};
    }
    return actual >= requested
        ? std::pair<int, int>{0, actual - requested}
        : std::pair<int, int>{1, requested - actual};
}

std::tuple<int, int, int> styleMatchRank(
    const wsc::FontDescriptor &descriptor, int requestedWeight,
    wsc::FontSlant requestedSlant)
{
    const auto weight = weightMatchRank(descriptor.weight, requestedWeight);
    return {slantMatchRank(descriptor.slant, requestedSlant),
            weight.first, weight.second};
}

bool validSource(const wsc::LazyFontSource &source)
{
    if (wsc::canonicalFontFamilyName(source.descriptor.family).empty()
        || source.sourceId.empty()) return false;
    for (const auto &range : source.codepointRanges) {
        if (range.first > range.last) return false;
    }
    for (const auto &coordinate : source.variationCoordinates) {
        if (coordinate.tag.size() != 4 || !std::isfinite(coordinate.value)) {
            return false;
        }
    }
    return true;
}

bool sameFloatBits(float left, float right)
{
    std::uint32_t leftBits = 0;
    std::uint32_t rightBits = 0;
    static_assert(sizeof(leftBits) == sizeof(left));
    std::memcpy(&leftBits, &left, sizeof(left));
    std::memcpy(&rightBits, &right, sizeof(right));
    return leftBits == rightBits;
}

bool sameLazySourceMetadata(const wsc::LazyFontSource &left,
                            const wsc::LazyFontSource &right)
{
    if (left.descriptor.family != right.descriptor.family
        || left.descriptor.weight != right.descriptor.weight
        || left.descriptor.slant != right.descriptor.slant
        || left.sourceId != right.sourceId
        || left.fingerprint != right.fingerprint
        || left.faceIndex != right.faceIndex
        || left.codepointRanges.size() != right.codepointRanges.size()
        || left.variationCoordinates.size()
            != right.variationCoordinates.size()) return false;
    for (std::size_t index = 0; index < left.codepointRanges.size(); ++index) {
        if (left.codepointRanges[index].first
                != right.codepointRanges[index].first
            || left.codepointRanges[index].last
                != right.codepointRanges[index].last) return false;
    }
    for (std::size_t index = 0;
         index < left.variationCoordinates.size(); ++index) {
        if (left.variationCoordinates[index].tag
                != right.variationCoordinates[index].tag
            || !sameFloatBits(left.variationCoordinates[index].value,
                              right.variationCoordinates[index].value)) {
            return false;
        }
    }
    return true;
}

bool sameRemoteSourceMetadata(const wsc::RemoteFontSource &left,
                              const wsc::RemoteFontSource &right)
{
    return left.expectedBytes == right.expectedBytes
        && left.priority == right.priority
        && sameLazySourceMetadata(left.font, right.font);
}

} // namespace

namespace wsc {

struct LazyFontProvider::Impl
{
    enum class LoadState
    {
        Pending,
        Loading,
        Loaded,
        Failed
    };

    struct Entry
    {
        LazyFontSource source;
        LoadState state = LoadState::Pending;
        std::unique_ptr<FontFace> face;
        bool active = true;
        std::uint64_t revision = 1;
    };

    FontProviderKind kind = FontProviderKind::ASSET;
    std::string name;
    Loader loader;
    mutable std::mutex mutex;
    mutable std::condition_variable loadCondition;
    mutable std::vector<std::unique_ptr<Entry>> entries;
    std::unordered_map<std::string, std::vector<std::size_t>> familyEntries;
    std::unordered_map<std::string, std::size_t> sourceEntries;
    std::unordered_map<std::string, std::uint64_t> familyGenerations;
    std::uint64_t generation = 1;

    void advance(const std::string &familyKey)
    {
        ++generation;
        if (generation == 0) generation = 1;
        auto &familyGeneration = familyGenerations[familyKey];
        ++familyGeneration;
        if (familyGeneration == 0) familyGeneration = 1;
    }

    static std::string sourceKey(const std::string &familyKey,
                                 const std::string &sourceId)
    {
        return familyKey + '\x1f' + sourceId;
    }

    void reset(Entry &entry) const
    {
        entry.face.reset();
        entry.state = LoadState::Pending;
        ++entry.revision;
        if (entry.revision == 0) entry.revision = 1;
        loadCondition.notify_all();
    }

    const FontFace *load(Entry &entry,
                         std::unique_lock<std::mutex> &lock) const
    {
        while (entry.state == LoadState::Loading) {
            loadCondition.wait(lock);
        }
        if (entry.state == LoadState::Loaded) return entry.face.get();
        if (!entry.active || entry.state == LoadState::Failed || !loader) {
            return nullptr;
        }

        entry.state = LoadState::Loading;
        const std::uint64_t revision = entry.revision;
        const LazyFontSource source = entry.source;
        lock.unlock();
        std::optional<std::vector<std::uint8_t>> bytes =
            loader(source.sourceId);
        std::unique_ptr<FontFace> loadedFace;
        if (bytes && !bytes->empty()) {
            FontFace face = FontFace::fromMemory(
                source.descriptor, std::move(*bytes),
                std::max(0, source.faceIndex));
            for (const FontCodepointRange &range : source.codepointRanges) {
                face.addCodepointRange(range.first, range.last);
            }
            for (const FontVariationCoordinate &coordinate :
                 source.variationCoordinates) {
                (void)face.setVariationCoordinate(
                    coordinate.tag, coordinate.value);
            }
            loadedFace = std::make_unique<FontFace>(std::move(face));
        }
        lock.lock();
        if (!entry.active || entry.revision != revision) {
            loadCondition.notify_all();
            return nullptr;
        }
        if (!loadedFace) {
            entry.state = LoadState::Failed;
            loadCondition.notify_all();
            return nullptr;
        }
        entry.face = std::move(loadedFace);
        entry.state = LoadState::Loaded;
        loadCondition.notify_all();
        return entry.face.get();
    }
};

LazyFontProvider::LazyFontProvider(FontProviderKind providerKind,
                                   std::string providerName, Loader loader)
    : impl_(std::make_unique<Impl>())
{
    impl_->kind = providerKind;
    impl_->name = std::move(providerName);
    impl_->loader = std::move(loader);
}

LazyFontProvider::~LazyFontProvider() = default;

FontProviderKind LazyFontProvider::kind() const { return impl_->kind; }
const std::string &LazyFontProvider::name() const { return impl_->name; }

std::uint64_t LazyFontProvider::generation() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->generation;
}

std::uint64_t LazyFontProvider::generationForFamily(
    const std::string &family) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyGenerations.find(
        canonicalFontFamilyName(family));
    return found == impl_->familyGenerations.end() ? 0 : found->second;
}

void LazyFontProvider::refresh()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto &[familyKey, indices] : impl_->familyEntries) {
        for (std::size_t index : indices) {
            if (index < impl_->entries.size() && impl_->entries[index]->active) {
                impl_->reset(*impl_->entries[index]);
            }
        }
        impl_->advance(familyKey);
    }
}

bool LazyFontProvider::hasFamily(const std::string &family) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyEntries.find(
        canonicalFontFamilyName(family));
    return found != impl_->familyEntries.end() && !found->second.empty();
}

std::vector<std::string> LazyFontProvider::families() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::string> result;
    result.reserve(impl_->familyEntries.size());
    for (const auto &[familyKey, indices] : impl_->familyEntries) {
        (void)familyKey;
        if (!indices.empty()) {
            result.push_back(
                impl_->entries[indices.front()]->source.descriptor.family);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const std::string &left, const std::string &right) {
                  return canonicalFontFamilyName(left)
                      < canonicalFontFamilyName(right);
              });
    return result;
}

std::string LazyFontProvider::displayFamilyName(
    const std::string &family) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyEntries.find(
        canonicalFontFamilyName(family));
    if (found == impl_->familyEntries.end() || found->second.empty()) {
        return family;
    }
    return impl_->entries[found->second.front()]->source.descriptor.family;
}

std::vector<const FontFace *> LazyFontProvider::match(
    const FontMatchRequest &request) const
{
    std::unique_lock<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyEntries.find(
        canonicalFontFamilyName(request.family));
    if (found == impl_->familyEntries.end()) return {};

    std::vector<std::size_t> indices = found->second;
    std::stable_sort(indices.begin(), indices.end(),
                     [&](std::size_t left, std::size_t right) {
        return styleMatchRank(impl_->entries[left]->source.descriptor,
                              request.weight, request.slant)
            < styleMatchRank(impl_->entries[right]->source.descriptor,
                             request.weight, request.slant);
    });
    std::vector<const FontFace *> result;
    result.reserve(indices.size());
    for (std::size_t index : indices) {
        Impl::Entry &entry = *impl_->entries[index];
        if (!entry.active) continue;
        if (const FontFace *face = impl_->load(entry, lock)) {
            result.push_back(face);
        }
    }
    return result;
}

bool LazyFontProvider::registerSource(LazyFontSource source)
{
    if (!validSource(source) || !impl_->loader) return false;
    source.faceIndex = std::max(0, source.faceIndex);
    const std::string familyKey = canonicalFontFamilyName(
        source.descriptor.family);
    const std::string sourceKey = Impl::sourceKey(familyKey, source.sourceId);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (const auto found = impl_->sourceEntries.find(sourceKey);
        found != impl_->sourceEntries.end()) {
        Impl::Entry &entry = *impl_->entries[found->second];
        if (!source.fingerprint.empty()
            && sameLazySourceMetadata(entry.source, source)) return true;
        entry.source = std::move(source);
        entry.active = true;
        impl_->reset(entry);
        impl_->advance(familyKey);
        return true;
    }

    const std::size_t index = impl_->entries.size();
    auto entry = std::make_unique<Impl::Entry>();
    entry->source = std::move(source);
    impl_->entries.push_back(std::move(entry));
    impl_->familyEntries[familyKey].push_back(index);
    impl_->sourceEntries.emplace(sourceKey, index);
    impl_->advance(familyKey);
    return true;
}

bool LazyFontProvider::invalidateFamily(const std::string &family)
{
    const std::string familyKey = canonicalFontFamilyName(family);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyEntries.find(familyKey);
    if (found == impl_->familyEntries.end()) return false;
    for (std::size_t index : found->second) {
        impl_->reset(*impl_->entries[index]);
    }
    impl_->advance(familyKey);
    return true;
}

bool LazyFontProvider::removeFamily(const std::string &family)
{
    const std::string familyKey = canonicalFontFamilyName(family);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyEntries.find(familyKey);
    if (found == impl_->familyEntries.end()) return false;
    for (std::size_t index : found->second) {
        Impl::Entry &entry = *impl_->entries[index];
        impl_->sourceEntries.erase(Impl::sourceKey(
            familyKey, entry.source.sourceId));
        entry.active = false;
        entry.face.reset();
    }
    impl_->familyEntries.erase(found);
    impl_->advance(familyKey);
    return true;
}

std::size_t LazyFontProvider::sourceCount() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->sourceEntries.size();
}

std::size_t LazyFontProvider::loadedFaceCount() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return static_cast<std::size_t>(std::count_if(
        impl_->entries.begin(), impl_->entries.end(),
        [](const auto &entry) {
            return entry->active && entry->state == Impl::LoadState::Loaded;
        }));
}

struct RemoteFontProvider::Impl
{
    struct Entry
    {
        RemoteFontSource source;
        RemoteFontState state = RemoteFontState::IDLE;
        std::size_t attempts = 0;
        std::uint64_t requestToken = 0;
        std::size_t reservedBytes = 0;
        std::unique_ptr<FontFace> face;
        bool active = true;
    };

    FontProviderKind kind = FontProviderKind::DYNAMIC;
    std::string name;
    RemoteFontProviderOptions options;
    mutable std::mutex mutex;
    mutable std::vector<std::unique_ptr<Entry>> entries;
    mutable std::deque<std::size_t> queue;
    std::unordered_map<std::string, std::size_t> sourceEntries;
    std::unordered_map<std::string, std::vector<std::size_t>> familyEntries;
    std::unordered_map<std::string, std::uint64_t> familyGenerations;
    std::unordered_map<std::string, std::string> changedFamilies;
    std::uint64_t generation = 1;
    std::size_t activeDownloads = 0;
    std::size_t reservedBytes = 0;
    std::size_t downloadedBytes = 0;
    std::uint64_t nextRequestToken = 1;

    static std::size_t addSaturated(std::size_t left, std::size_t right)
    {
        const std::size_t maximum = std::numeric_limits<std::size_t>::max();
        return right > maximum - left ? maximum : left + right;
    }

    void advance(const std::string &familyKey,
                 const std::string &displayFamily = {})
    {
        ++generation;
        if (generation == 0) generation = 1;
        auto &familyGeneration = familyGenerations[familyKey];
        ++familyGeneration;
        if (familyGeneration == 0) familyGeneration = 1;
        std::string display = displayFamily;
        if (display.empty()) {
            const auto found = familyEntries.find(familyKey);
            if (found != familyEntries.end() && !found->second.empty()) {
                display = entries[found->second.front()]
                    ->source.font.descriptor.family;
            }
        }
        changedFamilies.insert_or_assign(
            familyKey, display.empty() ? familyKey : std::move(display));
    }

    void releaseReservation(Entry &entry)
    {
        reservedBytes = entry.reservedBytes > reservedBytes
            ? 0 : reservedBytes - entry.reservedBytes;
        entry.reservedBytes = 0;
    }

    void reset(Entry &entry)
    {
        if (entry.state == RemoteFontState::DOWNLOADING && activeDownloads > 0) {
            --activeDownloads;
        }
        releaseReservation(entry);
        entry.face.reset();
        entry.state = RemoteFontState::IDLE;
        entry.attempts = 0;
        entry.requestToken = 0;
    }

    bool withinBudget(std::size_t extra) const
    {
        if (options.downloadBudgetBytes == 0) return true;
        const std::size_t used = addSaturated(downloadedBytes, reservedBytes);
        return extra <= options.downloadBudgetBytes
            && used <= options.downloadBudgetBytes - extra;
    }

    bool queueEntry(std::size_t index) const
    {
        Entry &entry = *entries[index];
        if (!entry.active || entry.state != RemoteFontState::IDLE) return false;
        if (!withinBudget(entry.source.expectedBytes)) {
            entry.state = RemoteFontState::PERMANENT_FAILURE;
            const_cast<Impl *>(this)->advance(canonicalFontFamilyName(
                entry.source.font.descriptor.family));
            return false;
        }
        entry.reservedBytes = entry.source.expectedBytes;
        const_cast<Impl *>(this)->reservedBytes = addSaturated(
            reservedBytes, entry.reservedBytes);
        entry.state = RemoteFontState::QUEUED;
        queue.push_back(index);
        return true;
    }

    static bool covers(const Entry &entry, std::uint32_t codepoint)
    {
        const auto &ranges = entry.source.font.codepointRanges;
        return ranges.empty() || std::any_of(
            ranges.begin(), ranges.end(), [&](const FontCodepointRange &range) {
                return codepoint >= range.first && codepoint <= range.last;
            });
    }
};

RemoteFontProvider::RemoteFontProvider(FontProviderKind providerKind,
                                       std::string providerName,
                                       RemoteFontProviderOptions options)
    : impl_(std::make_unique<Impl>())
{
    impl_->kind = providerKind;
    impl_->name = std::move(providerName);
    options.maxConcurrentDownloads = std::max<std::size_t>(
        1, options.maxConcurrentDownloads);
    options.maxAttemptsPerSource = std::max<std::size_t>(
        1, options.maxAttemptsPerSource);
    options.maxCandidatesPerMatch = std::max<std::size_t>(
        1, options.maxCandidatesPerMatch);
    impl_->options = options;
}

RemoteFontProvider::~RemoteFontProvider() = default;

FontProviderKind RemoteFontProvider::kind() const { return impl_->kind; }
const std::string &RemoteFontProvider::name() const { return impl_->name; }

std::uint64_t RemoteFontProvider::generation() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->generation;
}

std::uint64_t RemoteFontProvider::generationForFamily(
    const std::string &family) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyGenerations.find(
        canonicalFontFamilyName(family));
    return found == impl_->familyGenerations.end() ? 0 : found->second;
}

void RemoteFontProvider::refresh()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto &[familyKey, indices] : impl_->familyEntries) {
        for (std::size_t index : indices) {
            if (index < impl_->entries.size() && impl_->entries[index]->active) {
                impl_->reset(*impl_->entries[index]);
            }
        }
        impl_->advance(familyKey);
    }
}

bool RemoteFontProvider::hasFamily(const std::string &family) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyEntries.find(
        canonicalFontFamilyName(family));
    return found != impl_->familyEntries.end() && !found->second.empty();
}

std::vector<std::string> RemoteFontProvider::families() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::string> result;
    result.reserve(impl_->familyEntries.size());
    for (const auto &[familyKey, indices] : impl_->familyEntries) {
        (void)familyKey;
        if (!indices.empty()) {
            result.push_back(impl_->entries[indices.front()]
                                 ->source.font.descriptor.family);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const std::string &left, const std::string &right) {
                  return canonicalFontFamilyName(left)
                      < canonicalFontFamilyName(right);
              });
    return result;
}

std::string RemoteFontProvider::displayFamilyName(
    const std::string &family) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyEntries.find(
        canonicalFontFamilyName(family));
    if (found == impl_->familyEntries.end() || found->second.empty()) {
        return family;
    }
    return impl_->entries[found->second.front()]
        ->source.font.descriptor.family;
}

std::vector<const FontFace *> RemoteFontProvider::match(
    const FontMatchRequest &request) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyEntries.find(
        canonicalFontFamilyName(request.family));
    if (found == impl_->familyEntries.end()) return {};

    std::vector<std::size_t> indices = found->second;
    std::stable_sort(indices.begin(), indices.end(),
                     [&](std::size_t left, std::size_t right) {
        const Impl::Entry &leftEntry = *impl_->entries[left];
        const Impl::Entry &rightEntry = *impl_->entries[right];
        const auto leftStyle = styleMatchRank(
            leftEntry.source.font.descriptor, request.weight, request.slant);
        const auto rightStyle = styleMatchRank(
            rightEntry.source.font.descriptor, request.weight, request.slant);
        if (leftStyle != rightStyle) return leftStyle < rightStyle;
        if (leftEntry.source.priority != rightEntry.source.priority) {
            return leftEntry.source.priority < rightEntry.source.priority;
        }
        return leftEntry.source.font.sourceId
            < rightEntry.source.font.sourceId;
    });

    std::vector<const FontFace *> result;
    for (std::size_t index : indices) {
        const Impl::Entry &entry = *impl_->entries[index];
        if (entry.active && entry.state == RemoteFontState::LOADED
            && entry.face) result.push_back(entry.face.get());
    }

    if (request.codepoints.empty()) {
        const bool alreadyAvailableOrPending = std::any_of(
            indices.begin(), indices.end(), [&](std::size_t index) {
                const RemoteFontState state = impl_->entries[index]->state;
                return state == RemoteFontState::LOADED
                    || state == RemoteFontState::QUEUED
                    || state == RemoteFontState::DOWNLOADING;
            });
        if (!alreadyAvailableOrPending) {
            for (std::size_t index : indices) {
                if (impl_->queueEntry(index)) break;
            }
        }
        return result;
    }

    std::vector<std::uint32_t> remaining = request.codepoints;
    remaining.erase(std::unique(remaining.begin(), remaining.end()),
                    remaining.end());
    for (std::size_t index : indices) {
        const Impl::Entry &entry = *impl_->entries[index];
        if (!entry.active || (entry.state != RemoteFontState::LOADED
            && entry.state != RemoteFontState::QUEUED
            && entry.state != RemoteFontState::DOWNLOADING)) continue;
        remaining.erase(std::remove_if(
            remaining.begin(), remaining.end(), [&](std::uint32_t codepoint) {
                return Impl::covers(entry, codepoint);
            }), remaining.end());
    }

    std::size_t selected = 0;
    while (!remaining.empty()
           && selected < impl_->options.maxCandidatesPerMatch) {
        std::optional<std::size_t> best;
        std::size_t bestCoverage = 0;
        for (std::size_t index : indices) {
            const Impl::Entry &entry = *impl_->entries[index];
            if (!entry.active || entry.state != RemoteFontState::IDLE) continue;
            const std::size_t coverage = static_cast<std::size_t>(std::count_if(
                remaining.begin(), remaining.end(),
                [&](std::uint32_t codepoint) {
                    return Impl::covers(entry, codepoint);
                }));
            if (coverage == 0) continue;
            if (!best || coverage > bestCoverage) {
                best = index;
                bestCoverage = coverage;
            }
        }
        if (!best) break;
        Impl::Entry &entry = *impl_->entries[*best];
        if (!impl_->queueEntry(*best)) continue;
        ++selected;
        remaining.erase(std::remove_if(
            remaining.begin(), remaining.end(), [&](std::uint32_t codepoint) {
                return Impl::covers(entry, codepoint);
            }), remaining.end());
    }
    return result;
}

bool RemoteFontProvider::registerSource(RemoteFontSource source)
{
    if (!validSource(source.font)) return false;
    source.font.faceIndex = std::max(0, source.font.faceIndex);
    const std::string familyKey = canonicalFontFamilyName(
        source.font.descriptor.family);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (const auto found = impl_->sourceEntries.find(source.font.sourceId);
        found != impl_->sourceEntries.end()) {
        const std::size_t index = found->second;
        Impl::Entry &entry = *impl_->entries[index];
        if (!source.font.fingerprint.empty()
            && sameRemoteSourceMetadata(entry.source, source)) return true;
        const std::string oldDisplayFamily =
            entry.source.font.descriptor.family;
        const std::string oldFamilyKey = canonicalFontFamilyName(
            oldDisplayFamily);
        impl_->reset(entry);
        if (oldFamilyKey != familyKey) {
            auto &oldIndices = impl_->familyEntries[oldFamilyKey];
            oldIndices.erase(std::remove(oldIndices.begin(), oldIndices.end(), index),
                             oldIndices.end());
            if (oldIndices.empty()) impl_->familyEntries.erase(oldFamilyKey);
            impl_->familyEntries[familyKey].push_back(index);
            impl_->advance(oldFamilyKey, oldDisplayFamily);
        }
        entry.source = std::move(source);
        entry.active = true;
        impl_->advance(familyKey);
        return true;
    }

    const std::size_t index = impl_->entries.size();
    auto entry = std::make_unique<Impl::Entry>();
    entry->source = std::move(source);
    impl_->sourceEntries.emplace(entry->source.font.sourceId, index);
    impl_->entries.push_back(std::move(entry));
    impl_->familyEntries[familyKey].push_back(index);
    impl_->advance(familyKey);
    return true;
}

bool RemoteFontProvider::removeFamily(const std::string &family)
{
    const std::string familyKey = canonicalFontFamilyName(family);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyEntries.find(familyKey);
    if (found == impl_->familyEntries.end()) return false;
    const std::string displayFamily = found->second.empty()
        ? family : impl_->entries[found->second.front()]
            ->source.font.descriptor.family;
    for (std::size_t index : found->second) {
        Impl::Entry &entry = *impl_->entries[index];
        impl_->reset(entry);
        entry.active = false;
        impl_->sourceEntries.erase(entry.source.font.sourceId);
    }
    impl_->familyEntries.erase(found);
    impl_->advance(familyKey, displayFamily);
    return true;
}

bool RemoteFontProvider::invalidateFamily(const std::string &family)
{
    const std::string familyKey = canonicalFontFamilyName(family);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->familyEntries.find(familyKey);
    if (found == impl_->familyEntries.end()) return false;
    for (std::size_t index : found->second) {
        impl_->reset(*impl_->entries[index]);
    }
    impl_->advance(familyKey);
    return true;
}

std::vector<RemoteFontRequest> RemoteFontProvider::takeDownloadRequests()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<RemoteFontRequest> result;
    while (impl_->activeDownloads < impl_->options.maxConcurrentDownloads
           && !impl_->queue.empty()) {
        const std::size_t index = impl_->queue.front();
        impl_->queue.pop_front();
        if (index >= impl_->entries.size()) continue;
        Impl::Entry &entry = *impl_->entries[index];
        if (!entry.active || entry.state != RemoteFontState::QUEUED) continue;
        entry.state = RemoteFontState::DOWNLOADING;
        ++entry.attempts;
        ++impl_->activeDownloads;
        entry.requestToken = impl_->nextRequestToken++;
        if (impl_->nextRequestToken == 0) impl_->nextRequestToken = 1;
        result.push_back({entry.source.font.sourceId,
                          entry.source.font.descriptor.family,
                          entry.source.expectedBytes, entry.attempts,
                          entry.requestToken});
    }
    return result;
}

std::vector<std::string> RemoteFontProvider::takeChangedFamilies()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::string> result;
    result.reserve(impl_->changedFamilies.size());
    for (const auto &[familyKey, displayFamily] : impl_->changedFamilies) {
        (void)familyKey;
        result.push_back(displayFamily);
    }
    impl_->changedFamilies.clear();
    std::sort(result.begin(), result.end(),
              [](const std::string &left, const std::string &right) {
                  return canonicalFontFamilyName(left)
                      < canonicalFontFamilyName(right);
              });
    return result;
}

bool RemoteFontProvider::completeDownload(const std::string &sourceId,
                                          std::uint64_t requestToken,
                                          std::vector<std::uint8_t> bytes)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->sourceEntries.find(sourceId);
    if (found == impl_->sourceEntries.end()) return false;
    Impl::Entry &entry = *impl_->entries[found->second];
    if (!entry.active || entry.state != RemoteFontState::DOWNLOADING
        || requestToken == 0 || entry.requestToken != requestToken) {
        return false;
    }
    if (impl_->activeDownloads > 0) --impl_->activeDownloads;
    impl_->releaseReservation(entry);
    impl_->downloadedBytes = Impl::addSaturated(
        impl_->downloadedBytes, bytes.size());
    const std::string familyKey = canonicalFontFamilyName(
        entry.source.font.descriptor.family);
    if (bytes.empty() || (impl_->options.downloadBudgetBytes != 0
        && impl_->downloadedBytes > impl_->options.downloadBudgetBytes)) {
        entry.requestToken = 0;
        entry.state = RemoteFontState::PERMANENT_FAILURE;
        impl_->advance(familyKey);
        return false;
    }

    FontFace face = FontFace::fromMemory(
        entry.source.font.descriptor, std::move(bytes),
        entry.source.font.faceIndex);
    for (const FontCodepointRange &range : entry.source.font.codepointRanges) {
        face.addCodepointRange(range.first, range.last);
    }
    for (const FontVariationCoordinate &coordinate :
         entry.source.font.variationCoordinates) {
        (void)face.setVariationCoordinate(coordinate.tag, coordinate.value);
    }
    entry.face = std::make_unique<FontFace>(std::move(face));
    entry.requestToken = 0;
    entry.state = RemoteFontState::LOADED;
    impl_->advance(familyKey);
    return true;
}

bool RemoteFontProvider::failDownload(const std::string &sourceId,
                                      std::uint64_t requestToken,
                                      bool permanent,
                                      std::size_t consumedBytes)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->sourceEntries.find(sourceId);
    if (found == impl_->sourceEntries.end()) return false;
    Impl::Entry &entry = *impl_->entries[found->second];
    if (!entry.active || entry.state != RemoteFontState::DOWNLOADING
        || requestToken == 0 || entry.requestToken != requestToken) {
        return false;
    }
    if (impl_->activeDownloads > 0) --impl_->activeDownloads;
    impl_->releaseReservation(entry);
    impl_->downloadedBytes = Impl::addSaturated(
        impl_->downloadedBytes, consumedBytes);
    const bool budgetExceeded = impl_->options.downloadBudgetBytes != 0
        && impl_->downloadedBytes > impl_->options.downloadBudgetBytes;
    const bool exhausted = entry.attempts >= impl_->options.maxAttemptsPerSource;
    entry.requestToken = 0;
    if (permanent || budgetExceeded || exhausted) {
        entry.state = RemoteFontState::PERMANENT_FAILURE;
        impl_->advance(canonicalFontFamilyName(
            entry.source.font.descriptor.family));
    } else {
        entry.state = RemoteFontState::IDLE;
    }
    return true;
}

RemoteFontState RemoteFontProvider::state(const std::string &sourceId) const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->sourceEntries.find(sourceId);
    return found == impl_->sourceEntries.end()
        ? RemoteFontState::UNKNOWN
        : impl_->entries[found->second]->state;
}

std::size_t RemoteFontProvider::queuedCount() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return static_cast<std::size_t>(std::count_if(
        impl_->entries.begin(), impl_->entries.end(), [](const auto &entry) {
            return entry->active && entry->state == RemoteFontState::QUEUED;
        }));
}

std::size_t RemoteFontProvider::downloadingCount() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->activeDownloads;
}

std::size_t RemoteFontProvider::downloadedBytes() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->downloadedBytes;
}

} // namespace wsc
