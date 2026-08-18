#include "text/platform/AndroidFontProvider.h"
#include "text/platform/AndroidFontConfig.h"
#include "text/FontRasterizer.h"
#include "text/TextUtils.h"

#include "wsc/FontResolver.h"

#if defined(__ANDROID__)

#include <dlfcn.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct AFontMatcher;
struct AFont;

struct AndroidFontApi
{
    using CreateMatcher = AFontMatcher *(*)();
    using DestroyMatcher = void (*)(AFontMatcher *);
    using SetStyle = void (*)(AFontMatcher *, std::uint16_t, bool);
    using SetLocales = void (*)(AFontMatcher *, const char *);
    using Match = AFont *(*)(const AFontMatcher *, const char *,
                             const std::uint16_t *, std::uint32_t,
                             std::uint32_t *);
    using CloseFont = void (*)(AFont *);
    using GetPath = const char *(*)(const AFont *);
    using GetWeight = std::uint16_t (*)(const AFont *);
    using IsItalic = bool (*)(const AFont *);
    using GetCollectionIndex = std::size_t (*)(const AFont *);
    using GetAxisCount = std::size_t (*)(const AFont *);
    using GetAxisTag = std::uint32_t (*)(const AFont *, std::uint32_t);
    using GetAxisValue = float (*)(const AFont *, std::uint32_t);

    AndroidFontApi()
    {
        library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) return;
        createMatcher = load<CreateMatcher>("AFontMatcher_create");
        destroyMatcher = load<DestroyMatcher>("AFontMatcher_destroy");
        setStyle = load<SetStyle>("AFontMatcher_setStyle");
        setLocales = load<SetLocales>("AFontMatcher_setLocales");
        match = load<Match>("AFontMatcher_match");
        closeFont = load<CloseFont>("AFont_close");
        getPath = load<GetPath>("AFont_getFontFilePath");
        getWeight = load<GetWeight>("AFont_getWeight");
        isItalic = load<IsItalic>("AFont_isItalic");
        getCollectionIndex = load<GetCollectionIndex>("AFont_getCollectionIndex");
        getAxisCount = load<GetAxisCount>("AFont_getAxisCount");
        getAxisTag = load<GetAxisTag>("AFont_getAxisTag");
        getAxisValue = load<GetAxisValue>("AFont_getAxisValue");
    }

    ~AndroidFontApi()
    {
        if (library != nullptr) dlclose(library);
    }

    AndroidFontApi(const AndroidFontApi &) = delete;
    AndroidFontApi &operator=(const AndroidFontApi &) = delete;

    bool available() const
    {
        return createMatcher != nullptr && destroyMatcher != nullptr
            && setStyle != nullptr && setLocales != nullptr && match != nullptr
            && closeFont != nullptr && getPath != nullptr
            && getWeight != nullptr && isItalic != nullptr
            && getCollectionIndex != nullptr;
    }

    void *library = nullptr;
    CreateMatcher createMatcher = nullptr;
    DestroyMatcher destroyMatcher = nullptr;
    SetStyle setStyle = nullptr;
    SetLocales setLocales = nullptr;
    Match match = nullptr;
    CloseFont closeFont = nullptr;
    GetPath getPath = nullptr;
    GetWeight getWeight = nullptr;
    IsItalic isItalic = nullptr;
    GetCollectionIndex getCollectionIndex = nullptr;
    GetAxisCount getAxisCount = nullptr;
    GetAxisTag getAxisTag = nullptr;
    GetAxisValue getAxisValue = nullptr;

private:
    template<typename Function>
    Function load(const char *symbol)
    {
        return reinterpret_cast<Function>(dlsym(library, symbol));
    }
};

std::optional<std::string> androidGenericFamily(const std::string &family)
{
    const std::string key = wsc::canonicalFontFamilyName(family);
    if (key == wsc::canonicalFontFamilyName(wsc::FontSystem::kDefaultSerifFamily)
        || key == "serif") {
        return "serif";
    }
    if (key == wsc::canonicalFontFamilyName(wsc::FontSystem::kDefaultMonoFamily)
        || key == "monospace") {
        return "monospace";
    }
    if (key == "cursive" || key == "fantasy") return key;
    static const std::unordered_set<std::string> sansFamilies = {
        wsc::canonicalFontFamilyName(wsc::FontSystem::kDefaultPrimaryFamily),
        wsc::canonicalFontFamilyName(wsc::FontSystem::kDefaultCjkFamily),
        wsc::canonicalFontFamilyName(wsc::FontSystem::kDefaultArabicFamily),
        wsc::canonicalFontFamilyName(wsc::FontSystem::kDefaultHebrewFamily),
        wsc::canonicalFontFamilyName(wsc::FontSystem::kDefaultSymbolFamily),
        "androidsans", "androidcjk", "sans-serif", "system-ui"
    };
    if (sansFamilies.find(key) != sansFamilies.end()) return "sans-serif";
    return std::nullopt;
}

bool readableFile(const std::string &path)
{
    std::ifstream stream(path, std::ios::binary);
    return stream.good();
}

std::shared_ptr<const std::vector<std::uint8_t>> readFontSnapshot(
    const std::string &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return nullptr;
    stream.seekg(0, std::ios::end);
    const std::streamoff length = stream.tellg();
    // Android's current system fonts are far below this. Keep a hard ceiling
    // because matcher output is platform-controlled and the snapshot is held
    // for the provider generation.
    constexpr std::streamoff kMaxFontBytes = 128 * 1024 * 1024;
    if (length <= 0 || length > kMaxFontBytes) return nullptr;
    stream.seekg(0, std::ios::beg);
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(
        static_cast<std::size_t>(length));
    if (!stream.read(reinterpret_cast<char *>(bytes->data()), length)) {
        return nullptr;
    }
    return bytes;
}

std::optional<std::string> readTextFile(const std::string &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    stream.seekg(0, std::ios::end);
    const std::streamoff length = stream.tellg();
    constexpr std::streamoff kMaxConfigBytes = 8 * 1024 * 1024;
    if (length < 0 || length > kMaxConfigBytes) return std::nullopt;
    stream.seekg(0, std::ios::beg);
    std::string text(static_cast<std::size_t>(length), '\0');
    if (length > 0 && !stream.read(text.data(), length)) return std::nullopt;
    return text;
}

std::string matchCacheKey(const wsc::FontMatchRequest &request,
                          const std::string &genericFamily)
{
    std::string key = genericFamily + '\x1f'
        + std::to_string(std::clamp(request.weight, 1, 1000)) + '\x1f'
        + std::to_string(static_cast<int>(request.slant)) + '\x1f'
        + request.locale;
    for (std::uint32_t codepoint : request.codepoints) {
        key += '\x1e' + std::to_string(codepoint);
    }
    return key;
}

class AndroidSystemFontProvider final : public wsc::FontProvider
{
public:
    AndroidSystemFontProvider()
    {
        loadFileBackedSources();
    }

    wsc::FontProviderKind kind() const override
    {
        return wsc::FontProviderKind::SYSTEM;
    }

    const std::string &name() const override { return name_; }

    std::uint64_t generation() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return generation_;
    }

    void refresh() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        matchCache_.clear();
        matchedFaces_.clear();
        faceIndexByKey_.clear();
        loadFileBackedSources();
        ++generation_;
        if (generation_ == 0) generation_ = 1;
    }

    bool hasFamily(const std::string &family) const override
    {
        if (androidGenericFamily(family).has_value()) return true;
        std::lock_guard<std::mutex> lock(mutex_);
        return wsc::text::detail::hasAndroidFontConfigFamily(fontConfig_, family);
    }

    std::vector<const wsc::FontFace *> match(
        const wsc::FontMatchRequest &request) const override
    {
        const auto genericFamily = androidGenericFamily(request.family);

        std::lock_guard<std::mutex> lock(mutex_);
        const std::string configuredFamily = genericFamily
            ? *genericFamily : request.family;
        if (!genericFamily
            && !wsc::text::detail::hasAndroidFontConfigFamily(
                fontConfig_, configuredFamily)) {
            return {};
        }
        const std::string key = matchCacheKey(request, configuredFamily);
        const auto cached = matchCache_.find(key);
        if (cached != matchCache_.end()) return resolveCached(cached->second);

        std::vector<std::size_t> indices;
        if (api_.available() && genericFamily) {
            const auto matched = matchWithApi(request, configuredFamily);
            if (matched) {
                indices.push_back(internFace(std::move(*matched)));
            }
        }
        appendConfiguredCandidates(configuredFamily, request, indices);
        if (genericFamily) {
            appendLegacyCandidates(configuredFamily, request.family, indices);
        }
        if (matchCache_.size() >= kMaxMatchCacheEntries) matchCache_.clear();
        matchCache_.emplace(key, indices);
        return resolveCached(indices);
    }

private:
    static constexpr std::size_t kMaxMatchCacheEntries = 4096;

    std::optional<wsc::FontFace> matchWithApi(
        const wsc::FontMatchRequest &request,
        const std::string &genericFamily) const
    {
        AFontMatcher *matcher = api_.createMatcher();
        if (matcher == nullptr) return std::nullopt;

        const int requestedWeight = std::clamp(request.weight, 1, 1000);
        api_.setStyle(matcher, static_cast<std::uint16_t>(requestedWeight),
                      request.slant != wsc::FontSlant::NORMAL);
        if (!request.locale.empty()) api_.setLocales(matcher, request.locale.c_str());

        std::vector<std::uint16_t> text =
            wsc::text::encodeCodepointsToUtf16(request.codepoints);
        if (text.empty()) text.push_back(0x20u);
        std::uint32_t runLength = 0;
        AFont *font = api_.match(matcher, genericFamily.c_str(), text.data(),
                                 static_cast<std::uint32_t>(text.size()), &runLength);
        std::optional<wsc::FontFace> result;
        if (font != nullptr && runLength == text.size()) {
            const char *path = api_.getPath(font);
            const std::size_t collectionIndex = api_.getCollectionIndex(font);
            if (path != nullptr && path[0] != '\0'
                && collectionIndex <= static_cast<std::size_t>(
                    std::numeric_limits<int>::max())) {
                const int weight = std::clamp(
                    static_cast<int>(api_.getWeight(font)), 1, 1000);
                const wsc::FontSlant slant = api_.isItalic(font)
                    ? wsc::FontSlant::ITALIC : wsc::FontSlant::NORMAL;
                const std::string sourcePath(path);
                const auto bytes = snapshotFont(sourcePath);
                if (bytes) {
                    // AFont_getFontFilePath is only needed while the returned
                    // AFont is alive. Snapshot now so shaping/rasterization is
                    // independent of a stable or later-readable filesystem
                    // path (OTA/overlay/OEM private layout changes included).
                    result = wsc::FontFace::fromSharedMemory(
                        wsc::FontDescriptor(request.family, weight, slant),
                        bytes, static_cast<int>(collectionIndex),
                        "android-system:" + sourcePath);
                } else if (readableFile(sourcePath)) {
                    // Preserve compatibility for an unexpectedly huge font;
                    // ordinary Android system fonts take the snapshot path.
                    result = wsc::FontFace::fromFile(
                        wsc::FontDescriptor(request.family, weight, slant),
                        sourcePath, static_cast<int>(collectionIndex));
                }
                if (!result) {
                    if (font != nullptr) api_.closeFont(font);
                    api_.destroyMatcher(matcher);
                    return std::nullopt;
                }
                if (api_.getAxisCount != nullptr && api_.getAxisTag != nullptr
                    && api_.getAxisValue != nullptr) {
                    const std::size_t axisCount = api_.getAxisCount(font);
                    for (std::size_t axisIndex = 0;
                         axisIndex < axisCount
                             && axisIndex <= static_cast<std::size_t>(
                                 std::numeric_limits<std::uint32_t>::max());
                         ++axisIndex) {
                        const std::uint32_t tagValue = api_.getAxisTag(
                            font, static_cast<std::uint32_t>(axisIndex));
                        std::string tag(4, '\0');
                        tag[0] = static_cast<char>((tagValue >> 24u) & 0xFFu);
                        tag[1] = static_cast<char>((tagValue >> 16u) & 0xFFu);
                        tag[2] = static_cast<char>((tagValue >> 8u) & 0xFFu);
                        tag[3] = static_cast<char>(tagValue & 0xFFu);
                        result->setVariationCoordinate(
                            std::move(tag), api_.getAxisValue(
                                font, static_cast<std::uint32_t>(axisIndex)));
                    }
                }
            }
        }
        if (font != nullptr) api_.closeFont(font);
        api_.destroyMatcher(matcher);
        return result;
    }

    void addLegacyFace(const std::string &genericFamily,
                       const std::string &path)
    {
        if (!readableFile(path)) return;
        legacyFaces_[genericFamily].push_back(wsc::FontFace::fromFile(
            wsc::FontDescriptor(genericFamily), path));
    }

    void loadFileBackedSources()
    {
        fontConfig_ = {};
        legacyFaces_.clear();
        fontSnapshots_.clear();

        const auto loadConfig = [&](const char *path, const char *fontDirectory) {
            const auto xml = readTextFile(path);
            return xml && wsc::text::detail::parseAndroidFontConfig(
                *xml, fontDirectory, fontConfig_);
        };
        const bool modernSystem = loadConfig(
            "/system/etc/fonts.xml", "/system/fonts");
        if (!modernSystem) {
            loadConfig("/system/etc/system_fonts.xml", "/system/fonts");
            loadConfig("/system/etc/fallback_fonts.xml", "/system/fonts");
        }
        loadConfig("/vendor/etc/fonts.xml", "/vendor/fonts");
        loadConfig("/product/etc/fonts.xml", "/product/fonts");
        loadConfig("/product/etc/fonts_customization.xml", "/product/fonts");

        // Android XML may omit weight/style and expects the font scanner to
        // recover them from the file. Hydrate before candidate sorting so a
        // 700 italic face is not ranked as the 400 upright default.
        for (auto &face : fontConfig_.faces) {
            if (!face.weightSpecified || !face.slantSpecified) {
                (void)wsc::text::detail::resolveAndroidFontConfigFaceStyle(face);
            }
        }

        // These paths are intentionally last-resort compatibility candidates
        // for damaged/minimal configs and older vendor images.
        addLegacyFace("sans-serif", "/system/fonts/Roboto-Regular.ttf");
        addLegacyFace("sans-serif", "/system/fonts/NotoSansCJK-Regular.ttc");
        addLegacyFace("sans-serif", "/product/fonts/NotoSansCJK-Regular.ttc");
        addLegacyFace("sans-serif", "/system/fonts/NotoColorEmoji.ttf");
        addLegacyFace("serif", "/system/fonts/NotoSerif-Regular.ttf");
        addLegacyFace("monospace", "/system/fonts/RobotoMono-Regular.ttf");
    }

    std::shared_ptr<const std::vector<std::uint8_t>> snapshotFont(
        const std::string &path) const
    {
        const auto found = fontSnapshots_.find(path);
        if (found != fontSnapshots_.end()) return found->second;
        auto bytes = readFontSnapshot(path);
        if (bytes) fontSnapshots_.emplace(path, bytes);
        return bytes;
    }

    void appendConfiguredCandidates(
        const std::string &genericFamily,
        const wsc::FontMatchRequest &request,
        std::vector<std::size_t> &indices) const
    {
        const wsc::text::EmojiPresentation presentation =
            wsc::text::classifyEmojiPresentation(request.codepoints);
        const auto candidates = wsc::text::detail::matchAndroidFontConfig(
            fontConfig_, genericFamily, request.weight, request.slant,
            request.locale,
            presentation == wsc::text::EmojiPresentation::Emoji,
            presentation == wsc::text::EmojiPresentation::Text);
        for (const auto *candidate : candidates) {
            if (candidate == nullptr || !readableFile(candidate->path)) continue;
            wsc::FontFace face = wsc::FontFace::fromFile(
                wsc::FontDescriptor(request.family, candidate->weight,
                                    candidate->slant),
                candidate->path, candidate->faceIndex);
            for (const auto &axis : candidate->variationAxes) {
                face.setVariationCoordinate(axis.tag, axis.value);
            }
            const std::size_t index = internFace(std::move(face));
            if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
                indices.push_back(index);
            }
        }
    }

    void appendLegacyCandidates(const std::string &genericFamily,
                                const std::string &requestedFamily,
                                std::vector<std::size_t> &indices) const
    {
        const auto found = legacyFaces_.find(genericFamily);
        if (found == legacyFaces_.end()) return;
        for (const wsc::FontFace &legacy : found->second) {
            bool duplicate = false;
            for (std::size_t index : indices) {
                const wsc::FontFace &candidate = *matchedFaces_[index];
                duplicate = duplicate
                    || (candidate.path() == legacy.path()
                        && candidate.faceIndex() == legacy.faceIndex());
            }
            if (duplicate) continue;
            indices.push_back(internFace(wsc::FontFace::fromFile(
                wsc::FontDescriptor(requestedFamily, legacy.weight(),
                                    legacy.slant()),
                legacy.path(), legacy.faceIndex())));
        }
    }

    std::size_t internFace(wsc::FontFace face) const
    {
        const std::string key = wsc::canonicalFontFamilyName(face.family())
            + '\x1f' + wsc::text::fontFaceIdentity(face) + '\x1f'
            + std::to_string(face.weight()) + '\x1f'
            + std::to_string(static_cast<int>(face.slant()));
        const auto found = faceIndexByKey_.find(key);
        if (found != faceIndexByKey_.end()) return found->second;
        const std::size_t index = matchedFaces_.size();
        matchedFaces_.push_back(std::make_unique<wsc::FontFace>(std::move(face)));
        faceIndexByKey_.emplace(key, index);
        return index;
    }

    std::vector<const wsc::FontFace *> resolveCached(
        const std::vector<std::size_t> &indices) const
    {
        std::vector<const wsc::FontFace *> result;
        result.reserve(indices.size());
        for (std::size_t index : indices) {
            if (index < matchedFaces_.size()) result.push_back(matchedFaces_[index].get());
        }
        return result;
    }

    const std::string name_ = "android-system";
    AndroidFontApi api_;
    wsc::text::detail::AndroidFontConfig fontConfig_;
    std::unordered_map<std::string, std::vector<wsc::FontFace>> legacyFaces_;
    mutable std::mutex mutex_;
    mutable std::uint64_t generation_ = 1;
    mutable std::vector<std::unique_ptr<wsc::FontFace>> matchedFaces_;
    mutable std::unordered_map<std::string, std::size_t> faceIndexByKey_;
    mutable std::unordered_map<std::string, std::vector<std::size_t>> matchCache_;
    mutable std::unordered_map<
        std::string, std::shared_ptr<const std::vector<std::uint8_t>>>
        fontSnapshots_;
};

} // namespace

#endif // defined(__ANDROID__)

namespace wsc::text {

std::shared_ptr<FontProvider> createAndroidSystemFontProvider()
{
#if defined(__ANDROID__)
    return std::make_shared<AndroidSystemFontProvider>();
#else
    return nullptr;
#endif
}

} // namespace wsc::text
