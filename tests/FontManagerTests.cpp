#include <wsc/FontSystem.h>

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "wsc/Font.h"
#include "wsc/FontResolver.h"
#include "text/FontRasterizer.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

bool testRegisterFontFile()
{
    wsc::FontManager manager;
    const bool registered = manager.registerFontFile(wsc::FontDescriptor("Inter", 400), "fonts/Inter-Regular.ttf");
    const auto *face = manager.findFirstFace("Inter");

    return expect(registered, "valid file font should register")
        && expect(manager.hasFamily("Inter"), "registered family should be discoverable")
        && expect(face != nullptr, "registered face should be found")
        && expect(face->sourceType() == wsc::FontSourceType::FILE, "file font should keep source type")
        && expect(face->path() == "fonts/Inter-Regular.ttf", "file font should keep path");
}

bool testRegisterFontMemory()
{
    wsc::FontManager manager;
    std::vector<std::uint8_t> bytes = {0, 1, 2, 3};
    const bool registered = manager.registerFontMemory(wsc::FontDescriptor("MemoryFace"), bytes);
    const auto *face = manager.findFirstFace("MemoryFace");

    return expect(registered, "valid memory font should register")
        && expect(face != nullptr, "memory face should be found")
        && expect(face->sourceType() == wsc::FontSourceType::MEMORY, "memory font should keep source type")
        && expect(face->bytes() != nullptr && face->bytes()->size() == 4, "memory font should keep bytes");
}

bool testSharedMemoryPlatformSource()
{
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>(
        std::initializer_list<std::uint8_t>{0, 1, 2, 3});
    const wsc::FontFace first = wsc::FontFace::fromSharedMemory(
        wsc::FontDescriptor("Platform Snapshot"), bytes, 2,
        "android-system:opaque-font-42");
    const wsc::FontFace same = wsc::FontFace::fromSharedMemory(
        wsc::FontDescriptor("Platform Snapshot"), bytes, 2,
        "android-system:opaque-font-42");
    const wsc::FontFace otherFace = wsc::FontFace::fromSharedMemory(
        wsc::FontDescriptor("Platform Snapshot"), bytes, 3,
        "android-system:opaque-font-42");

    return expect(first.isValid() && first.sourceType() == wsc::FontSourceType::MEMORY,
                  "a platform snapshot should be a valid memory-backed face")
        && expect(first.path().empty()
                      && first.sourceId() == "android-system:opaque-font-42",
                  "a platform snapshot should not expose its source id as a file path")
        && expect(first.bytes() == bytes.get(),
                  "a platform snapshot should retain immutable shared bytes without copying")
        && expect(wsc::text::fontFaceIdentity(first)
                      == wsc::text::fontFaceIdentity(same),
                  "the same shared platform snapshot should have stable identity")
        && expect(wsc::text::fontFaceIdentity(first)
                      != wsc::text::fontFaceIdentity(otherFace),
                  "platform snapshot identity should preserve collection face index");
}

bool testSharedMemoryPlatformSourceRasterization()
{
    std::ifstream input(WHATSCANVAS_TEST_SHARED_SOURCE_FONT, std::ios::binary);
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> fontBytes(
        length > 0 ? static_cast<std::size_t>(length) : 0u);
    if (!fontBytes.empty()) {
        input.read(reinterpret_cast<char *>(fontBytes.data()), length);
    }
    auto sharedBytes = std::make_shared<const std::vector<std::uint8_t>>(
        std::move(fontBytes));
    const wsc::FontFace face = wsc::FontFace::fromSharedMemory(
        wsc::FontDescriptor("Platform Snapshot"), sharedBytes, 0,
        "android-system:matcher-result");
    wsc::text::FontRasterizer rasterizer;
    const auto data = rasterizer.fontData(face);

    return expect(input.good() || input.eof(),
                  "the shared-source raster fixture should be readable")
        && expect(!sharedBytes->empty(),
                  "the shared-source raster fixture should contain bytes")
        && expect(face.path().empty() && rasterizer.hasGlyph(face, 'A'),
                  "a platform snapshot should rasterize without a filesystem path")
        && expect(data.has_value() && data->size == sharedBytes->size(),
                  "the rasterizer should consume the complete shared snapshot")
        && expect(data->data == sharedBytes->data(),
                  "the rasterizer should retain shared font bytes without copying");
}

bool testFontFaceCodepointRanges()
{
    wsc::FontFace face = wsc::FontFace::fromFile(wsc::FontDescriptor("Fallback"), "fallback.ttf");
    face.addCodepointRange(0x1F300, 0x1FAFF);
    face.addCodepointRange(126, 32);

    return expect(face.hasCodepointRanges(), "font face should report declared ranges")
        && expect(face.codepointRanges().size() == 1, "invalid codepoint ranges should be ignored")
        && expect(face.supportsCodepoint(0x1F600), "font face should match codepoints inside a range")
        && expect(!face.supportsCodepoint('A'), "font face should reject codepoints outside declared ranges");
}

bool testFontFaceVariationCoordinates()
{
    wsc::FontFace face = wsc::FontFace::fromFile(
        wsc::FontDescriptor("Variable"), "variable.ttf");
    const bool first = face.setVariationCoordinate("wght", 425.0f);
    const bool replaced = face.setVariationCoordinate("wght", 500.0f);
    const bool second = face.setVariationCoordinate("wdth", 90.5f);
    const bool invalidTag = face.setVariationCoordinate("bad", 1.0f);
    const bool invalidValue = face.setVariationCoordinate(
        "opsz", std::numeric_limits<float>::infinity());
    return expect(first && replaced && second,
                  "valid variation coordinates should be accepted")
        && expect(!invalidTag && !invalidValue,
                  "invalid variation coordinates should be rejected")
        && expect(face.variationCoordinates().size() == 2
                      && face.variationCoordinates()[0].tag == "wght"
                      && face.variationCoordinates()[0].value == 500.0f
                      && face.variationCoordinates()[1].tag == "wdth",
                  "axis updates should replace by tag without changing order");
}

bool testFontFaceVariationIdentity()
{
    wsc::FontFace first = wsc::FontFace::fromFile(
        wsc::FontDescriptor("Variable"), "variable.ttf", 2);
    first.setVariationCoordinate("wght", 500.0f);
    first.setVariationCoordinate("wdth", 90.0f);

    wsc::FontFace reordered = wsc::FontFace::fromFile(
        wsc::FontDescriptor("Variable"), "variable.ttf", 2);
    reordered.setVariationCoordinate("wdth", 90.0f);
    reordered.setVariationCoordinate("wght", 500.0f);

    wsc::FontFace adjacentValue = wsc::FontFace::fromFile(
        wsc::FontDescriptor("Variable"), "variable.ttf", 2);
    adjacentValue.setVariationCoordinate(
        "wght", std::nextafter(500.0f, std::numeric_limits<float>::infinity()));
    adjacentValue.setVariationCoordinate("wdth", 90.0f);

    wsc::FontFace otherCollectionFace = wsc::FontFace::fromFile(
        wsc::FontDescriptor("Variable"), "variable.ttf", 3);
    otherCollectionFace.setVariationCoordinate("wght", 500.0f);
    otherCollectionFace.setVariationCoordinate("wdth", 90.0f);

    const std::string firstIdentity = wsc::text::fontFaceIdentity(first);
    return expect(firstIdentity == wsc::text::fontFaceIdentity(reordered),
                  "variation identity should not depend on coordinate insertion order")
        && expect(wsc::text::fontVariationIdentity(
                      first.variationCoordinates())
                      == wsc::text::fontVariationIdentity(
                          reordered.variationCoordinates()),
                  "paint and backend cache identity should use the same canonical axis set")
        && expect(firstIdentity != wsc::text::fontFaceIdentity(adjacentValue),
                  "variation identity should preserve the exact float bit value")
        && expect(firstIdentity != wsc::text::fontFaceIdentity(otherCollectionFace),
                  "variation identity should preserve the collection face index");
}

bool testFontFaceCollectionIndex()
{
    wsc::FontFace defaultFile = wsc::FontFace::fromFile(wsc::FontDescriptor("Default"), "collection.ttc");
    wsc::FontFace indexedFile = wsc::FontFace::fromFile(wsc::FontDescriptor("Indexed"), "collection.ttc", 2);
    wsc::FontFace clampedFile = wsc::FontFace::fromFile(wsc::FontDescriptor("Clamped"), "collection.ttc", -3);
    wsc::FontFace indexedMemory =
        wsc::FontFace::fromMemory(wsc::FontDescriptor("MemoryIndexed"), {1, 2, 3, 4}, 1);
    wsc::FontManager manager;
    manager.registerFontFile(wsc::FontDescriptor("ManagedFile"), "collection.ttc", 3);
    manager.registerFontMemory(wsc::FontDescriptor("ManagedMemory"), {5, 6, 7, 8}, 4);
    const wsc::FontFace *managedFile = manager.findFirstFace("ManagedFile");
    const wsc::FontFace *managedMemory = manager.findFirstFace("ManagedMemory");

    return expect(defaultFile.faceIndex() == 0, "file font should default to collection face 0")
        && expect(indexedFile.faceIndex() == 2, "file font should preserve explicit collection face index")
        && expect(clampedFile.faceIndex() == 0, "negative file face index should clamp to 0")
        && expect(indexedMemory.faceIndex() == 1, "memory font should preserve explicit collection face index")
        && expect(managedFile != nullptr && managedFile->faceIndex() == 3,
                  "font manager file registration should preserve collection face index")
        && expect(managedMemory != nullptr && managedMemory->faceIndex() == 4,
                  "font manager memory registration should preserve collection face index");
}

bool testFallbackResolutionOrder()
{
    wsc::FontManager manager;
    manager.registerFontFile(wsc::FontDescriptor("Primary"), "primary.ttf");
    manager.registerFontFile(wsc::FontDescriptor("CJK"), "cjk.otf");
    manager.registerFontFile(wsc::FontDescriptor("Fallback"), "fallback.ttf");

    const bool firstFallback = manager.addFallbackFamily("Primary", "CJK");
    const bool secondFallback = manager.addFallbackFamily("Primary", "Fallback");
    manager.addFallbackFamily("Primary", "CJK");
    const auto families = manager.resolveFamilies("Primary");

    return expect(firstFallback && secondFallback, "registered families should form fallback chain")
        && expect(families.size() == 3, "fallback chain should include primary and unique fallbacks")
        && expect(families[0] == "Primary", "primary family should resolve first")
        && expect(families[1] == "CJK", "first fallback should resolve second")
        && expect(families[2] == "Fallback", "second fallback should resolve third")
        && expect(!manager.addFallbackFamily("Primary", "Missing"), "missing fallback should be rejected");
}

bool testBestFaceMatching()
{
    wsc::FontManager manager;
    manager.registerFontFile(wsc::FontDescriptor("Family", 400), "regular.ttf");
    manager.registerFontFile(wsc::FontDescriptor("Family", 700), "bold.ttf");
    manager.registerFontFile(wsc::FontDescriptor("Family", 400, wsc::FontSlant::ITALIC), "italic.ttf");

    const wsc::FontFace *regular = manager.findBestFace("Family", 450, wsc::FontSlant::NORMAL);
    const wsc::FontFace *bold = manager.findBestFace("Family", 760, wsc::FontSlant::NORMAL);
    const wsc::FontFace *italic = manager.findBestFace("Family", 700, wsc::FontSlant::ITALIC);

    return expect(regular != nullptr && regular->path() == "regular.ttf",
                  "best face matching should choose nearest regular weight")
        && expect(bold != nullptr && bold->path() == "bold.ttf",
                  "best face matching should choose nearest bold weight")
        && expect(italic != nullptr && italic->path() == "italic.ttf",
                  "best face matching should prefer requested slant before weight");
}

bool testCanonicalFamilyMatching()
{
    wsc::FontManager manager;
    manager.registerFontFile(wsc::FontDescriptor("Noto Sans CJK", 400), "cjk.ttf");
    manager.registerFontFile(wsc::FontDescriptor("Fallback", 400), "fallback.ttf");
    const bool fallbackAdded = manager.addFallbackFamily(" noto   sans cjk ", "FALLBACK");
    const auto families = manager.resolveFamilies("NOTO SANS CJK");

    return expect(manager.hasFamily("noto sans cjk"),
                  "family lookup should ignore ASCII case")
        && expect(manager.findFirstFace("  NOTO   SANS CJK ") != nullptr,
                  "family lookup should trim and collapse whitespace")
        && expect(fallbackAdded, "canonical family names should work in fallback chains")
        && expect(families.size() == 2 && families[0] == "Noto Sans CJK"
                      && families[1] == "Fallback",
                  "resolution should preserve registered display family names");
}

bool testCssWeightMatching()
{
    wsc::FontManager manager;
    manager.registerFontFile(wsc::FontDescriptor("CSS", 300), "light.ttf");
    manager.registerFontFile(wsc::FontDescriptor("CSS", 400), "regular.ttf");
    manager.registerFontFile(wsc::FontDescriptor("CSS", 500), "medium.ttf");
    manager.registerFontFile(wsc::FontDescriptor("CSS", 600), "semibold.ttf");

    const wsc::FontFace *weight450 = manager.findBestFace("CSS", 450);
    const wsc::FontFace *weight500WithoutExact = nullptr;
    wsc::FontManager sparse;
    sparse.registerFontFile(wsc::FontDescriptor("Sparse", 400), "regular.ttf");
    sparse.registerFontFile(wsc::FontDescriptor("Sparse", 600), "semibold.ttf");
    weight500WithoutExact = sparse.findBestFace("Sparse", 500);

    return expect(weight450 != nullptr && weight450->path() == "medium.ttf",
                  "CSS matching should search upward through 500 first")
        && expect(weight500WithoutExact != nullptr
                      && weight500WithoutExact->path() == "regular.ttf",
                  "CSS matching should prefer 400 over 600 for a missing 500 face");
}

class RecordingFontProvider final : public wsc::FontProvider
{
public:
    RecordingFontProvider(wsc::FontProviderKind providerKind, std::string providerName,
                          wsc::FontFace face)
        : kind_(providerKind), name_(std::move(providerName)), face_(std::move(face))
    {
    }

    wsc::FontProviderKind kind() const override { return kind_; }
    const std::string &name() const override { return name_; }
    std::uint64_t generation() const override { return generation_; }
    void refresh() override
    {
        ++refreshCount;
        ++generation_;
    }
    bool hasFamily(const std::string &family) const override
    {
        return wsc::canonicalFontFamilyName(family)
            == wsc::canonicalFontFamilyName(face_.family());
    }
    std::vector<const wsc::FontFace *> match(
        const wsc::FontMatchRequest &request) const override
    {
        lastRequest = request;
        return hasFamily(request.family)
            ? std::vector<const wsc::FontFace *>{&face_}
            : std::vector<const wsc::FontFace *>{};
    }

    mutable wsc::FontMatchRequest lastRequest;
    int refreshCount = 0;

private:
    wsc::FontProviderKind kind_;
    std::string name_;
    wsc::FontFace face_;
    std::uint64_t generation_ = 1;
};

bool testProviderRefreshContract()
{
    auto system = std::make_shared<RecordingFontProvider>(
        wsc::FontProviderKind::SYSTEM, "system",
        wsc::FontFace::fromFile(wsc::FontDescriptor("System"), "system.ttf"));
    auto dynamic = std::make_shared<RecordingFontProvider>(
        wsc::FontProviderKind::DYNAMIC, "dynamic",
        wsc::FontFace::fromFile(wsc::FontDescriptor("Dynamic"), "dynamic.ttf"));
    wsc::FontResolver resolver;
    resolver.addProvider(system);
    resolver.addProvider(dynamic);
    const std::uint64_t before = resolver.generation();

    resolver.refreshProviders(wsc::FontProviderKind::SYSTEM);

    return expect(system->refreshCount == 1,
                  "refresh should reach providers of the requested kind")
        && expect(dynamic->refreshCount == 0,
                  "refresh should not invalidate unrelated provider kinds")
        && expect(resolver.generation() != before,
                  "provider refresh should change resolver generation");
}

bool testProviderPriorityAndRequestContext()
{
    auto dynamicFonts = std::make_shared<wsc::FontManager>();
    auto systemFonts = std::make_shared<wsc::FontManager>();
    dynamicFonts->registerFontFile(wsc::FontDescriptor("Shared", 400), "dynamic.ttf");
    systemFonts->registerFontFile(wsc::FontDescriptor("Shared", 400), "system.ttf");

    wsc::FontResolver resolver;
    // Add in reverse order to prove that kind, not insertion order, controls
    // same-family provider precedence.
    resolver.addProvider(std::make_shared<wsc::FontManagerProvider>(
        systemFonts, wsc::FontProviderKind::SYSTEM, "system"));
    resolver.addProvider(std::make_shared<wsc::FontManagerProvider>(
        dynamicFonts, wsc::FontProviderKind::DYNAMIC, "dynamic"));

    wsc::FontMatchRequest request;
    request.family = "shared";
    request.weight = 400;
    request.locale = "zh-CN";
    request.codepoints = {0x4E2D};
    const wsc::FontMatchResult result = resolver.resolve(
        request, [](const wsc::FontFace &, const std::vector<std::uint32_t> &) {
            return true;
        });
    auto recording = std::make_shared<RecordingFontProvider>(
        wsc::FontProviderKind::TEST, "recording",
        wsc::FontFace::fromFile(wsc::FontDescriptor("Locale Probe"), "probe.ttf"));
    wsc::FontResolver recordingResolver;
    recordingResolver.addProvider(recording);
    request.family = "Locale Probe";
    request.codepoints = {0x9AA8, 0xFE0F};
    const auto recordedResult = recordingResolver.resolve(
        request, [](const wsc::FontFace &, const std::vector<std::uint32_t> &) {
            return true;
        });

    return expect(result && result.face->path() == "dynamic.ttf",
                  "dynamic provider should override system for the same family")
        && expect(result.providerKind == wsc::FontProviderKind::DYNAMIC
                      && result.providerName == "dynamic",
                  "resolution should report the winning provider")
        && expect(recordedResult && recording->lastRequest.locale == "zh-CN",
                  "resolver should forward locale to providers")
        && expect(recording->lastRequest.codepoints == request.codepoints,
                  "resolver should forward the complete character cluster");
}

bool testLazyFontProviderLoadingAndFamilyInvalidation()
{
    std::unordered_map<std::string, int> loadCounts;
    std::weak_ptr<wsc::LazyFontProvider> providerWeak;
    bool loaderReenteredProvider = false;
    auto provider = std::make_shared<wsc::LazyFontProvider>(
        wsc::FontProviderKind::ASSET, "assets",
        [&](const std::string &sourceId)
            -> std::optional<std::vector<std::uint8_t>> {
            if (const auto activeProvider = providerWeak.lock()) {
                loaderReenteredProvider = activeProvider->sourceCount() > 0;
            }
            ++loadCounts[sourceId];
            if (sourceId == "missing") return std::nullopt;
            return std::vector<std::uint8_t>{0, 1, 2, 3};
        });
    providerWeak = provider;

    wsc::LazyFontSource regular;
    regular.descriptor = wsc::FontDescriptor("Lazy A", 400);
    regular.sourceId = "a-regular";
    regular.fingerprint = "regular-v1";
    regular.codepointRanges.emplace_back('A', 'Z');
    wsc::LazyFontSource bold = regular;
    bold.descriptor.weight = 700;
    bold.sourceId = "a-bold";
    wsc::LazyFontSource other;
    other.descriptor = wsc::FontDescriptor("Lazy B", 400);
    other.sourceId = "b-regular";
    wsc::LazyFontSource missing;
    missing.descriptor = wsc::FontDescriptor("Lazy Missing", 400);
    missing.sourceId = "missing";

    bool ok = expect(provider->registerSource(regular)
                         && provider->registerSource(bold)
                         && provider->registerSource(other)
                         && provider->registerSource(missing),
                     "lazy font metadata should register without loading bytes");
    ok = expect(provider->sourceCount() == 4
                    && provider->loadedFaceCount() == 0
                    && loadCounts.empty(),
                "source registration should not invoke the loader") && ok;

    wsc::FontResolver resolver;
    resolver.addProvider(provider);
    wsc::FontFallbackChain chain("Lazy A");
    chain.addFallbackFamily("Lazy B");
    ok = expect(resolver.setFallbackChain(chain) && loadCounts.empty(),
                "fallback policy setup should use metadata without loading assets") && ok;

    wsc::FontMatchRequest request;
    request.family = "lazy a";
    request.weight = 700;
    request.allowFallback = false;
    const wsc::FontMatchResult first = resolver.resolve(request);
    ok = expect(first && first.face->weight() == 700
                    && first.providerKind == wsc::FontProviderKind::ASSET,
                "first family match should load sources and preserve style order") && ok;
    ok = expect(loadCounts["a-regular"] == 1 && loadCounts["a-bold"] == 1
                    && loadCounts["b-regular"] == 0
                    && provider->loadedFaceCount() == 2
                    && loaderReenteredProvider,
                "matching should load only its family and invoke callbacks outside the provider lock") && ok;

    const std::uint64_t aBefore = resolver.resolutionGeneration("Lazy A");
    const std::uint64_t bBefore = resolver.resolutionGeneration("Lazy B");
    ok = expect(provider->invalidateFamily("Lazy A"),
                "known lazy family should invalidate") && ok;
    ok = expect(resolver.resolutionGeneration("Lazy A") != aBefore
                    && resolver.resolutionGeneration("Lazy B") == bBefore,
                "family invalidation should not change unrelated resolution generations") && ok;
    const wsc::FontMatchResult reloaded = resolver.resolve(request);
    ok = expect(reloaded && loadCounts["a-regular"] == 2
                    && loadCounts["a-bold"] == 2,
                "invalidated family should reload on its next match") && ok;

    request.family = "Lazy Missing";
    const wsc::FontMatchResult failedFirst = resolver.resolve(request);
    const wsc::FontMatchResult failedCached = resolver.resolve(request);
    ok = expect(!failedFirst && !failedCached && loadCounts["missing"] == 1,
                "failed lazy loads should be memoized until invalidation") && ok;
    ok = expect(provider->invalidateFamily("Lazy Missing"),
                "failed family should be retryable after invalidation") && ok;
    (void)resolver.resolve(request);
    ok = expect(loadCounts["missing"] == 2,
                "invalidating a failed family should permit one new attempt") && ok;

    regular.descriptor.weight = 500;
    ok = expect(provider->registerSource(regular)
                    && provider->sourceCount() == 4,
                "same family/source id should replace metadata without duplication") && ok;
    request.family = "Lazy A";
    request.weight = 500;
    const wsc::FontMatchResult replaced = resolver.resolve(request);
    ok = expect(replaced && replaced.face->weight() == 500
                    && loadCounts["a-regular"] == 3,
                "replaced source should reload with its new descriptor") && ok;
    const std::uint64_t unchangedBefore =
        resolver.resolutionGeneration("Lazy A");
    ok = expect(provider->registerSource(regular)
                    && resolver.resolutionGeneration("Lazy A")
                        == unchangedBefore,
                "identical fingerprint metadata should preserve lazy state") && ok;
    (void)resolver.resolve(request);
    ok = expect(loadCounts["a-regular"] == 3,
                "unchanged fingerprint should not reload bytes") && ok;
    regular.fingerprint = "regular-v2";
    ok = expect(provider->registerSource(regular)
                    && resolver.resolutionGeneration("Lazy A")
                        != unchangedBefore,
                "a changed fingerprint should invalidate the lazy family") && ok;
    (void)resolver.resolve(request);
    ok = expect(loadCounts["a-regular"] == 4,
                "changed fingerprint should reload bytes on next match") && ok;
    return ok;
}

bool testRemoteFontProviderSchedulingAndBudget()
{
    wsc::RemoteFontProviderOptions options;
    options.maxConcurrentDownloads = 1;
    options.maxAttemptsPerSource = 2;
    options.maxCandidatesPerMatch = 3;
    options.downloadBudgetBytes = 100;
    auto provider = std::make_shared<wsc::RemoteFontProvider>(
        wsc::FontProviderKind::DYNAMIC, "remote", options);

    wsc::RemoteFontSource latin;
    latin.font.descriptor = wsc::FontDescriptor("Remote A", 400);
    latin.font.sourceId = "latin";
    latin.font.fingerprint = "latin-v1";
    latin.font.codepointRanges.emplace_back('A', 'Z');
    latin.expectedBytes = 20;
    wsc::RemoteFontSource cjk;
    cjk.font.descriptor = wsc::FontDescriptor("Remote A", 400);
    cjk.font.sourceId = "cjk";
    cjk.font.codepointRanges.emplace_back(0x4E00, 0x9FFF);
    cjk.expectedBytes = 30;
    wsc::RemoteFontSource oversized;
    oversized.font.descriptor = wsc::FontDescriptor("Remote A", 400);
    oversized.font.sourceId = "oversized";
    oversized.font.codepointRanges.emplace_back(0x1F300, 0x1FAFF);
    oversized.expectedBytes = 200;

    bool ok = expect(provider->registerSource(latin)
                         && provider->registerSource(cjk)
                         && provider->registerSource(oversized),
                     "remote source metadata should register without downloading");
    ok = expect(provider->takeChangedFamilies()
                    == std::vector<std::string>{"Remote A"}
                    && provider->takeChangedFamilies().empty(),
                "source registrations should coalesce into one drainable family change") && ok;
    ok = expect(provider->state("unknown") == wsc::RemoteFontState::UNKNOWN
                    && provider->families() == std::vector<std::string>{"Remote A"},
                "remote provider should expose stable metadata and unknown state") && ok;

    wsc::FontResolver resolver;
    resolver.addProvider(provider);
    wsc::FontMatchRequest request;
    request.family = "Remote A";
    request.codepoints = {'A'};
    const std::uint64_t beforeQueue = resolver.resolutionGeneration("Remote A");
    ok = expect(!resolver.resolve(request)
                    && provider->state("latin") == wsc::RemoteFontState::QUEUED
                    && provider->queuedCount() == 1,
                "a missing codepoint should queue its best remote subset") && ok;
    (void)resolver.resolve(request);
    ok = expect(provider->queuedCount() == 1
                    && resolver.resolutionGeneration("Remote A") == beforeQueue,
                "repeated matches should deduplicate pending work without invalidating caches") && ok;

    auto downloads = provider->takeDownloadRequests();
    ok = expect(downloads.size() == 1 && downloads.front().sourceId == "latin"
                    && downloads.front().attempt == 1
                    && downloads.front().requestToken != 0
                    && provider->state("latin") == wsc::RemoteFontState::DOWNLOADING,
                "the host should drain a bounded first download attempt") && ok;
    ok = expect(!provider->failDownload("latin",
                                        downloads.front().requestToken + 1,
                                        true, 5)
                    && provider->state("latin")
                        == wsc::RemoteFontState::DOWNLOADING,
                "a stale callback token must not mutate a newer attempt") && ok;
    ok = expect(provider->takeDownloadRequests().empty(),
                "active download concurrency should be enforced") && ok;
    ok = expect(provider->failDownload("latin", downloads.front().requestToken,
                                       false, 5)
                    && provider->state("latin") == wsc::RemoteFontState::IDLE,
                "a transient failure should return a source to idle") && ok;
    ok = expect(provider->takeChangedFamilies().empty(),
                "transient transport state should not request relayout") && ok;

    (void)resolver.resolve(request);
    downloads = provider->takeDownloadRequests();
    ok = expect(downloads.size() == 1 && downloads.front().attempt == 2,
                "a later match should retry a transient source") && ok;
    const std::uint64_t beforePermanent =
        resolver.resolutionGeneration("Remote A");
    ok = expect(provider->failDownload("latin", downloads.front().requestToken,
                                       false, 5)
                    && provider->state("latin")
                        == wsc::RemoteFontState::PERMANENT_FAILURE
                    && resolver.resolutionGeneration("Remote A")
                        != beforePermanent,
                "the retry cap should memoize failure and invalidate family caches") && ok;
    ok = expect(provider->takeChangedFamilies()
                    == std::vector<std::string>{"Remote A"},
                "permanent availability changes should request one host relayout") && ok;

    request.codepoints = {0x4E2D};
    (void)resolver.resolve(request);
    downloads = provider->takeDownloadRequests();
    ok = expect(downloads.size() == 1 && downloads.front().sourceId == "cjk",
                "coverage should select the matching remote subset") && ok;
    const std::uint64_t beforeComplete =
        resolver.resolutionGeneration("Remote A");
    ok = expect(provider->completeDownload("cjk", downloads.front().requestToken,
                                           {0, 1, 2, 3})
                    && provider->state("cjk") == wsc::RemoteFontState::LOADED
                    && resolver.resolutionGeneration("Remote A")
                        != beforeComplete,
                "successful bytes should publish a face and invalidate family caches") && ok;
    ok = expect(provider->takeChangedFamilies()
                    == std::vector<std::string>{"Remote A"},
                "font completion should expose a coalesced family change") && ok;
    const wsc::FontMatchResult cjkResult = resolver.resolve(request);
    ok = expect(cjkResult && cjkResult.face->sourceType()
                         == wsc::FontSourceType::MEMORY,
                "a completed remote source should resolve as a memory face") && ok;

    request.codepoints = {0x1F600};
    (void)resolver.resolve(request);
    ok = expect(provider->state("oversized")
                    == wsc::RemoteFontState::PERMANENT_FAILURE
                    && provider->takeDownloadRequests().empty()
                    && provider->downloadedBytes() == 14,
                "the cumulative transfer budget should reject oversized candidates") && ok;

    latin.font.descriptor.weight = 500;
    ok = expect(provider->registerSource(latin)
                    && provider->state("latin") == wsc::RemoteFontState::IDLE,
                "re-registering a source should clear permanent failure state") && ok;
    request.codepoints = {'A'};
    (void)provider->takeChangedFamilies();
    (void)resolver.resolve(request);
    const auto replacedAttempt = provider->takeDownloadRequests();
    ok = expect(provider->registerSource(latin)
                    && provider->state("latin")
                        == wsc::RemoteFontState::DOWNLOADING
                    && provider->takeChangedFamilies().empty(),
                "unchanged fingerprint should preserve an active request") && ok;
    latin.font.descriptor.weight = 600;
    latin.font.fingerprint = "latin-v2";
    ok = expect(replacedAttempt.size() == 1
                    && provider->registerSource(latin),
                "an active source should be replaceable") && ok;
    if (replacedAttempt.size() != 1) return false;
    (void)resolver.resolve(request);
    const auto currentAttempt = provider->takeDownloadRequests();
    ok = expect(currentAttempt.size() == 1
                    && currentAttempt.front().requestToken
                        != replacedAttempt.front().requestToken,
                "replacement should issue a distinct request token") && ok;
    if (currentAttempt.size() != 1) return false;
    ok = expect(!provider->completeDownload(
                        "latin", replacedAttempt.front().requestToken,
                        {0, 1, 2, 3})
                    && provider->state("latin")
                        == wsc::RemoteFontState::DOWNLOADING,
                "a late response from the replaced request must be ignored") && ok;
    ok = expect(provider->completeDownload(
                        "latin", currentAttempt.front().requestToken,
                        {0, 1, 2, 3})
                    && provider->state("latin") == wsc::RemoteFontState::LOADED,
                "the current replacement attempt should still complete") && ok;
    return ok;
}

bool testClusterCoverageFallbackAndGeneration()
{
    auto manager = std::make_shared<wsc::FontManager>();
    manager->registerFontFile(wsc::FontDescriptor("Primary"), "primary.ttf");
    manager->registerFontFile(wsc::FontDescriptor("Fallback"), "fallback.ttf");

    wsc::FontResolver resolver;
    resolver.addProvider(std::make_shared<wsc::FontManagerProvider>(
        manager, wsc::FontProviderKind::DYNAMIC, "dynamic"));
    wsc::FontFallbackChain chain("Primary");
    chain.addFallbackFamily("Fallback");
    const bool chainSet = resolver.setFallbackChain(chain);
    const std::uint64_t generationBefore = resolver.generation();

    wsc::FontMatchRequest request;
    request.family = "Primary";
    request.locale = "ja-JP";
    request.codepoints = {0x9AA8, 0xFE0F};
    const wsc::FontMatchResult result = resolver.resolve(
        request, [](const wsc::FontFace &face,
                    const std::vector<std::uint32_t> &cluster) {
            return face.path() == "fallback.ttf" && cluster.size() == 2;
        });
    manager->registerFontFile(wsc::FontDescriptor("Later"), "later.ttf");

    return expect(chainSet, "resolver should accept a cross-provider fallback policy")
        && expect(static_cast<bool>(result),
                  "resolver should find a face for the complete cluster")
        && expect(result.face != nullptr && result.face->path() == "fallback.ttf",
                  "resolver should reject partial primary coverage")
        && expect(result.usedFallback,
                  "resolver should report that the fallback family won")
        && expect(result.coverageVerified,
                  "coverage predicate success should be visible in diagnostics")
        && expect(resolver.generation() != generationBefore,
                  "provider mutation should change the resolver generation");
}

bool testDefaultSymbolFallbackCoversEmoji()
{
    const std::vector<wsc::FontFace> faces = wsc::FontSystem::defaultSystemFontFaces();
    if (faces.empty()) {
        return true;
    }

    const wsc::FontFace *symbol = nullptr;
    for (const wsc::FontFace &face : faces) {
        if (face.family() == wsc::FontSystem::kDefaultSymbolFamily) {
            symbol = &face;
            break;
        }
    }

    if (symbol == nullptr) {
        return true;
    }

    return expect(symbol->supportsCodepoint(0x1F4BB),
                  "default symbol fallback should cover emoji codepoints like the laptop glyph")
        && expect(symbol->supportsCodepoint(0x1F1E8),
                  "default symbol fallback should cover flag codepoints")
        && expect(symbol->supportsCodepoint(0x2705),
                  "default symbol fallback should keep common symbol coverage");
}

bool testCjkAliasFallsBackToSymbolEmojiFamily()
{
    const std::vector<wsc::FontFace> faces = wsc::FontSystem::defaultSystemFontFaces();
    if (faces.empty()) {
        return true;
    }

    bool hasCjk = false;
    bool hasSymbol = false;
    for (const wsc::FontFace &face : faces) {
        hasCjk = hasCjk || face.family() == wsc::FontSystem::kDefaultCjkFamily;
        hasSymbol = hasSymbol || face.family() == wsc::FontSystem::kDefaultSymbolFamily;
    }
    if (!hasCjk || !hasSymbol) {
        return true;
    }

    auto manager = std::make_shared<wsc::FontManager>();
    for (const wsc::FontFace &face : faces) {
        manager->registerFace(face);
    }
    wsc::FontResolver resolver;
    resolver.addProvider(std::make_shared<wsc::FontManagerProvider>(
        manager, wsc::FontProviderKind::SYSTEM, "system"));

    wsc::FontFallbackChain cjkChain(wsc::FontSystem::kDefaultCjkFamily);
    cjkChain.addFallbackFamily(wsc::FontSystem::kDefaultSymbolFamily);
    const bool chainSet = resolver.setFallbackChain(cjkChain);
    wsc::FontMatchRequest request;
    request.family = wsc::FontSystem::kDefaultCjkFamily;
    request.codepoints = {0x1F4BB};
    const wsc::FontMatchResult result = resolver.resolve(
        request, [](const wsc::FontFace &face, const std::vector<std::uint32_t> &) {
            return face.family() == wsc::FontSystem::kDefaultSymbolFamily;
        });

    return expect(chainSet,
                  "CJK alias should accept an explicit symbol fallback chain")
        && expect(static_cast<bool>(result),
                  "CJK family should resolve to a symbol fallback for emoji codepoints")
        && expect(result.face != nullptr && result.face->family() == wsc::FontSystem::kDefaultSymbolFamily,
                  "emoji codepoints from the CJK alias should land on the symbol fallback family");
}

bool testSystemFontFallbackChain()
{
    const wsc::FontFallbackChain chain = wsc::FontSystem::defaultFallbackChain();
    const std::vector<wsc::FontFace> faces = wsc::FontSystem::defaultSystemFontFaces();

    bool primarySeen = false;
    bool primarySemiboldSeen = false;
    for (const wsc::FontFace &face : faces) {
        primarySeen = primarySeen || face.family() == wsc::FontSystem::kDefaultPrimaryFamily;
        primarySemiboldSeen = primarySemiboldSeen
            || (face.family() == wsc::FontSystem::kDefaultPrimaryFamily
                && face.weight() == 600);
    }

    return expect(chain.primaryFamily() == wsc::FontSystem::kDefaultPrimaryFamily,
                  "system fallback chain should use the public default primary family")
        && expect(!chain.fallbackFamilies().empty(),
                  "system fallback chain should include fallback families")
        && expect(faces.empty() || primarySeen,
                  "discovered system font faces should include the default primary when any face is found")
#ifdef _WIN32
        && expect(primarySemiboldSeen,
                  "Windows system fonts should register Segoe UI Semibold as the exact 600-weight primary face");
#else
        ;
#endif
}

bool testSystemFontRefreshGeneration()
{
    (void)wsc::FontSystem::defaultSystemFontFaces();
    const std::uint64_t before = wsc::FontSystem::installedFontGeneration();
    const std::uint64_t refreshed = wsc::FontSystem::refreshInstalledFonts();
    const std::uint64_t after = wsc::FontSystem::installedFontGeneration();

    return expect(before > 0, "default font discovery should initialize a generation")
        && expect(refreshed == before + 1, "refresh should advance the font generation once")
        && expect(after == refreshed, "reported generation should match the refreshed snapshot");
}

} // namespace

int main()
{
    const bool ok = testRegisterFontFile()
        && testRegisterFontMemory()
        && testSharedMemoryPlatformSource()
        && testSharedMemoryPlatformSourceRasterization()
        && testFontFaceCodepointRanges()
        && testFontFaceVariationCoordinates()
        && testFontFaceVariationIdentity()
        && testFontFaceCollectionIndex()
        && testFallbackResolutionOrder()
        && testBestFaceMatching()
        && testCanonicalFamilyMatching()
        && testCssWeightMatching()
        && testProviderRefreshContract()
        && testProviderPriorityAndRequestContext()
        && testLazyFontProviderLoadingAndFamilyInvalidation()
        && testRemoteFontProviderSchedulingAndBudget()
        && testClusterCoverageFallbackAndGeneration()
        && testDefaultSymbolFallbackCoversEmoji()
        && testCjkAliasFallsBackToSymbolEmojiFamily()
        && testSystemFontFallbackChain()
        && testSystemFontRefreshGeneration();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
