#include <wsc/FontSystem.h>

// Cross-platform system font enumeration. Uses the OS's native font manager
// so consumers can find installed fonts by their real family names (e.g.
// "Menlo" on macOS, "Consolas" on Windows, "DejaVu Sans" on Linux) instead
// of relying on operating-system-specific font paths.

#include "text/SystemFontEnumerator.h"

#include "wsc/Font.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(__APPLE__)
#  include <CoreFoundation/CoreFoundation.h>
#  include <CoreText/CoreText.h>
#elif defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dwrite.h>
#  include <wrl/client.h>
#elif defined(__linux__) && defined(WHATSCANVAS_HAS_FONTCONFIG)
#  include <fontconfig/fontconfig.h>
#endif

namespace wsc {
namespace detail {

namespace {

#if defined(__APPLE__)

std::string cfStringToUtf8(CFStringRef ref)
{
    if (ref == nullptr) return {};
    if (const char *fast = CFStringGetCStringPtr(ref, kCFStringEncodingUTF8)) {
        return std::string(fast);
    }
    const CFIndex length = CFStringGetLength(ref);
    const CFIndex bufferSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::vector<char> buffer(static_cast<std::size_t>(bufferSize), 0);
    if (!CFStringGetCString(ref, buffer.data(), bufferSize, kCFStringEncodingUTF8)) {
        return {};
    }
    return std::string(buffer.data());
}

std::string cfUrlToPath(CFURLRef url)
{
    if (url == nullptr) return {};
    char buf[PATH_MAX];
    if (!CFURLGetFileSystemRepresentation(url, TRUE, reinterpret_cast<UInt8 *>(buf), PATH_MAX)) {
        return {};
    }
    return std::string(buf);
}

using AppleFaceIndexCache = std::unordered_map<std::string, std::unordered_map<std::string, int>>;

int appleFontFaceIndex(CTFontDescriptorRef descriptor, CFURLRef url,
                       const std::string &path, AppleFaceIndexCache &cache)
{
    if (descriptor == nullptr || url == nullptr) return 0;

    const std::size_t dot = path.find_last_of('.');
    std::string extension = dot == std::string::npos ? std::string() : path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".ttc" && extension != ".otc" && extension != ".dfont") return 0;

    CFStringRef targetName = static_cast<CFStringRef>(
        CTFontDescriptorCopyAttribute(descriptor, kCTFontNameAttribute));
    if (targetName == nullptr) return 0;
    const std::string target = cfStringToUtf8(targetName);
    CFRelease(targetName);
    if (target.empty()) return 0;

    auto cached = cache.find(path);
    if (cached == cache.end()) {
        std::unordered_map<std::string, int> indices;
        CFArrayRef fileDescriptors = CTFontManagerCreateFontDescriptorsFromURL(url);
        if (fileDescriptors != nullptr) {
            const CFIndex count = CFArrayGetCount(fileDescriptors);
            for (CFIndex index = 0; index < count; ++index) {
                CTFontDescriptorRef candidate = static_cast<CTFontDescriptorRef>(
                    CFArrayGetValueAtIndex(fileDescriptors, index));
                CFStringRef candidateName = candidate == nullptr ? nullptr : static_cast<CFStringRef>(
                    CTFontDescriptorCopyAttribute(candidate, kCTFontNameAttribute));
                const std::string name = cfStringToUtf8(candidateName);
                if (candidateName != nullptr) CFRelease(candidateName);
                if (!name.empty()) {
                    indices.emplace(name, static_cast<int>(std::clamp<CFIndex>(
                        index, 0, static_cast<CFIndex>(std::numeric_limits<int>::max()))));
                }
            }
            CFRelease(fileDescriptors);
        }
        cached = cache.emplace(path, std::move(indices)).first;
    }

    const auto match = cached->second.find(target);
    return match == cached->second.end() ? 0 : match->second;
}

std::vector<DiscoveredFontFace> discoverApple()
{
    std::vector<DiscoveredFontFace> out;
    AppleFaceIndexCache faceIndexCache;

    CFArrayRef families = CTFontManagerCopyAvailableFontFamilyNames();
    if (families == nullptr) return out;

    const CFIndex familyCount = CFArrayGetCount(families);
    for (CFIndex i = 0; i < familyCount; ++i) {
        CFStringRef familyName = static_cast<CFStringRef>(CFArrayGetValueAtIndex(families, i));
        if (familyName == nullptr) continue;

        CFMutableDictionaryRef attrs = CFDictionaryCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFDictionaryAddValue(attrs, kCTFontFamilyNameAttribute, familyName);
        CTFontDescriptorRef familyDesc = CTFontDescriptorCreateWithAttributes(attrs);
        CFRelease(attrs);

        CFArrayRef matches = CTFontDescriptorCreateMatchingFontDescriptors(familyDesc, nullptr);
        CFRelease(familyDesc);
        if (matches == nullptr) continue;

        const CFIndex matchCount = CFArrayGetCount(matches);
        for (CFIndex j = 0; j < matchCount; ++j) {
            CTFontDescriptorRef desc = static_cast<CTFontDescriptorRef>(CFArrayGetValueAtIndex(matches, j));
            if (desc == nullptr) continue;

            CFURLRef urlRef = static_cast<CFURLRef>(CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute));
            if (urlRef == nullptr) continue;
            std::string path = cfUrlToPath(urlRef);
            const int faceIndex = appleFontFaceIndex(desc, urlRef, path, faceIndexCache);
            CFRelease(urlRef);
            if (path.empty()) continue;

            DiscoveredFontFace face;
            face.family = cfStringToUtf8(familyName);
            face.path = std::move(path);
            face.faceIndex = faceIndex;
            face.weight = 400;
            face.italic = false;

            CFDictionaryRef traits = static_cast<CFDictionaryRef>(
                CTFontDescriptorCopyAttribute(desc, kCTFontTraitsAttribute));
            if (traits != nullptr) {
                if (CFNumberRef weightRef = static_cast<CFNumberRef>(
                        CFDictionaryGetValue(traits, kCTFontWeightTrait))) {
                    double w = 0.0;
                    CFNumberGetValue(weightRef, kCFNumberDoubleType, &w);
                    // CoreText weight is normalised to [-1, 1] where 0 is regular.
                    // Map to CSS-style 100..900.
                    if (w <= -0.5) face.weight = 200;
                    else if (w <= -0.25) face.weight = 300;
                    else if (w <= 0.1) face.weight = 400;
                    else if (w <= 0.3) face.weight = 500;
                    else if (w <= 0.45) face.weight = 600;
                    else if (w <= 0.6) face.weight = 700;
                    else if (w <= 0.75) face.weight = 800;
                    else face.weight = 900;
                }
                if (CFNumberRef symRef = static_cast<CFNumberRef>(
                        CFDictionaryGetValue(traits, kCTFontSymbolicTrait))) {
                    uint32_t sym = 0;
                    CFNumberGetValue(symRef, kCFNumberSInt32Type, &sym);
                    face.italic = (sym & kCTFontTraitItalic) != 0;
                }
                CFRelease(traits);
            }

            out.push_back(std::move(face));
        }
        CFRelease(matches);
    }
    CFRelease(families);

    // CTFontManagerCopyAvailableFontFamilyNames filters out the private
    // system UI family (whose CoreText name starts with a dot), so
    // "SF Pro" / "SF Pro Text" never appear in the enumeration. Query the
    // UI font explicitly for a handful of weights, take its family name
    // (typically ".AppleSystemUIFont" on modern macOS), and append it to
    // the discovery so preferred-family lookups can find it.
    struct UiFontWeight { CTFontUIFontType uiType; int cssWeight; };
    const UiFontWeight uiWeights[] = {
        {kCTFontUIFontSystem, 400},
        {kCTFontUIFontEmphasizedSystem, 700},
    };
    for (const UiFontWeight &uw : uiWeights) {
        CTFontRef uiFont = CTFontCreateUIFontForLanguage(uw.uiType, 0.0, nullptr);
        if (uiFont == nullptr) continue;
        CTFontDescriptorRef desc = CTFontCopyFontDescriptor(uiFont);
        CFRelease(uiFont);
        if (desc == nullptr) continue;

        CFStringRef familyRef = static_cast<CFStringRef>(
            CTFontDescriptorCopyAttribute(desc, kCTFontFamilyNameAttribute));
        CFURLRef urlRef = static_cast<CFURLRef>(CTFontDescriptorCopyAttribute(desc, kCTFontURLAttribute));
        if (familyRef != nullptr && urlRef != nullptr) {
            std::string family = cfStringToUtf8(familyRef);
            std::string path = cfUrlToPath(urlRef);
            if (!family.empty() && !path.empty()) {
                DiscoveredFontFace face;
                face.family = std::move(family);
                face.path = std::move(path);
                face.weight = uw.cssWeight;
                out.push_back(std::move(face));
            }
        }
        if (familyRef != nullptr) CFRelease(familyRef);
        if (urlRef != nullptr) CFRelease(urlRef);
        CFRelease(desc);
    }
    return out;
}

#elif defined(_WIN32)

std::string wideToUtf8(const std::wstring &w)
{
    if (w.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                            out.data(), size, nullptr, nullptr) != size) {
        return {};
    }
    return out;
}

std::wstring localizedString(IDWriteLocalizedStrings *strings)
{
    if (strings == nullptr || strings->GetCount() == 0) return {};

    UINT32 index = 0;
    BOOL exists = FALSE;
    wchar_t localeName[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) > 0) {
        strings->FindLocaleName(localeName, &index, &exists);
    }
    if (!exists) {
        strings->FindLocaleName(L"en-us", &index, &exists);
    }
    if (!exists) index = 0;

    UINT32 length = 0;
    if (FAILED(strings->GetStringLength(index, &length))) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1u, L'\0');
    if (FAILED(strings->GetString(index, value.data(), length + 1u))) return {};
    value.resize(length);
    return value;
}

std::string localFontFilePath(IDWriteFontFile *file)
{
    using Microsoft::WRL::ComPtr;
    if (file == nullptr) return {};

    const void *referenceKey = nullptr;
    UINT32 referenceKeySize = 0;
    if (FAILED(file->GetReferenceKey(&referenceKey, &referenceKeySize))
        || referenceKey == nullptr || referenceKeySize == 0) {
        return {};
    }

    ComPtr<IDWriteFontFileLoader> loader;
    if (FAILED(file->GetLoader(loader.GetAddressOf()))) return {};

    ComPtr<IDWriteLocalFontFileLoader> localLoader;
    if (FAILED(loader.As(&localLoader))) {
        // Downloadable/cloud fonts do not have a stable local path and cannot
        // be represented by FontFace::fromFile until Windows materializes them.
        return {};
    }

    UINT32 pathLength = 0;
    if (FAILED(localLoader->GetFilePathLengthFromKey(referenceKey, referenceKeySize,
                                                      &pathLength))
        || pathLength == 0) {
        return {};
    }

    std::wstring path(static_cast<std::size_t>(pathLength) + 1u, L'\0');
    if (FAILED(localLoader->GetFilePathFromKey(referenceKey, referenceKeySize,
                                                path.data(), pathLength + 1u))) {
        return {};
    }
    path.resize(pathLength);
    return wideToUtf8(path);
}

std::vector<DiscoveredFontFace> discoverWindows()
{
    using Microsoft::WRL::ComPtr;
    std::vector<DiscoveredFontFace> out;

    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(factory.GetAddressOf())))) {
        return out;
    }

    ComPtr<IDWriteFontCollection> systemCollection;
    // This API is intentionally dynamic: ask DirectWrite to refresh its cached
    // collection so fonts installed since the previous call are visible.
    if (FAILED(factory->GetSystemFontCollection(systemCollection.GetAddressOf(), TRUE))) {
        return out;
    }

    const UINT32 familyCount = systemCollection->GetFontFamilyCount();
    for (UINT32 i = 0; i < familyCount; ++i) {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(systemCollection->GetFontFamily(i, family.GetAddressOf()))) continue;

        ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(family->GetFamilyNames(names.GetAddressOf()))) continue;

        const std::string familyName = wideToUtf8(localizedString(names.Get()));
        if (familyName.empty()) continue;
        const UINT32 fontCount = family->GetFontCount();
        for (UINT32 f = 0; f < fontCount; ++f) {
            ComPtr<IDWriteFont> font;
            if (FAILED(family->GetFont(f, font.GetAddressOf()))) continue;

            ComPtr<IDWriteFontFace> face;
            if (FAILED(font->CreateFontFace(face.GetAddressOf()))) continue;

            UINT32 fileCount = 0;
            if (FAILED(face->GetFiles(&fileCount, nullptr))) continue;
            if (fileCount == 0) continue;
            std::vector<IDWriteFontFile *> files(fileCount, nullptr);
            if (FAILED(face->GetFiles(&fileCount, files.data()))) {
                for (IDWriteFontFile *file : files) if (file != nullptr) file->Release();
                continue;
            }

            std::string path;
            for (IDWriteFontFile *file : files) {
                if (path.empty()) path = localFontFilePath(file);
                if (file != nullptr) file->Release();
            }
            if (path.empty()) continue;

            DiscoveredFontFace fd;
            fd.family = familyName;
            fd.path = std::move(path);
            fd.faceIndex = static_cast<int>(face->GetIndex());
            fd.weight = std::clamp(static_cast<int>(font->GetWeight()), 1, 1000);
            fd.italic = font->GetStyle() != DWRITE_FONT_STYLE_NORMAL;
            out.push_back(std::move(fd));
        }
    }
    return out;
}

#elif defined(__linux__) && defined(WHATSCANVAS_HAS_FONTCONFIG)

int fontconfigWeightToCss(int weight)
{
    if (weight <= FC_WEIGHT_THIN) return 100;
    if (weight <= FC_WEIGHT_EXTRALIGHT) return 200;
    if (weight <= FC_WEIGHT_LIGHT) return 300;
    if (weight <= FC_WEIGHT_REGULAR) return 400;
    if (weight <= FC_WEIGHT_MEDIUM) return 500;
    if (weight <= FC_WEIGHT_SEMIBOLD) return 600;
    if (weight <= FC_WEIGHT_BOLD) return 700;
    if (weight <= FC_WEIGHT_EXTRABOLD) return 800;
    return 900;
}

std::vector<DiscoveredFontFace> discoverFontconfig()
{
    std::vector<DiscoveredFontFace> out;
    if (!FcInit()) return out;

    FcPattern *pattern = FcPatternCreate();
    FcObjectSet *objectSet = FcObjectSetBuild(FC_FAMILY, FC_FILE, FC_INDEX, FC_WEIGHT, FC_SLANT,
                                              static_cast<char *>(nullptr));
    FcFontSet *fontSet = FcFontList(nullptr, pattern, objectSet);
    if (fontSet != nullptr) {
        for (int i = 0; i < fontSet->nfont; ++i) {
            FcPattern *entry = fontSet->fonts[i];
            FcChar8 *fileStr = nullptr;
            int faceIndex = 0;
            int weight = FC_WEIGHT_REGULAR;
            int slant = FC_SLANT_ROMAN;
            if (FcPatternGetString(entry, FC_FILE, 0, &fileStr) != FcResultMatch) continue;
            FcPatternGetInteger(entry, FC_INDEX, 0, &faceIndex);
            FcPatternGetInteger(entry, FC_WEIGHT, 0, &weight);
            FcPatternGetInteger(entry, FC_SLANT, 0, &slant);

            const std::string path = reinterpret_cast<const char *>(fileStr);
            if (path.empty()) continue;

            std::unordered_set<std::string> entryFamilies;
            for (int familyIndex = 0;; ++familyIndex) {
                FcChar8 *familyStr = nullptr;
                if (FcPatternGetString(entry, FC_FAMILY, familyIndex, &familyStr) != FcResultMatch) break;
                std::string family = reinterpret_cast<const char *>(familyStr);
                if (family.empty() || !entryFamilies.insert(family).second) continue;

                DiscoveredFontFace fd;
                fd.family = std::move(family);
                fd.path = path;
                fd.faceIndex = faceIndex;
                fd.weight = fontconfigWeightToCss(weight);
                fd.italic = slant != FC_SLANT_ROMAN;
                out.push_back(std::move(fd));
            }
        }
        FcFontSetDestroy(fontSet);
    }
    if (objectSet != nullptr) FcObjectSetDestroy(objectSet);
    if (pattern != nullptr) FcPatternDestroy(pattern);
    return out;
}

#endif

} // namespace

std::vector<DiscoveredFontFace> discoverInstalledFontFaces()
{
#if defined(__APPLE__)
    return discoverApple();
#elif defined(_WIN32)
    return discoverWindows();
#elif defined(__linux__) && defined(WHATSCANVAS_HAS_FONTCONFIG)
    return discoverFontconfig();
#else
    return {};
#endif
}

} // namespace detail
} // namespace wsc

namespace wsc {

namespace {

bool familyNameEquals(const std::string &lhs, const std::string &rhs)
{
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const auto left = static_cast<unsigned char>(lhs[i]);
        const auto right = static_cast<unsigned char>(rhs[i]);
        if (std::tolower(left) != std::tolower(right)) return false;
    }
    return true;
}

const FontFace *findPreferredFace(const std::vector<FontFace> &installed,
                                  std::initializer_list<const char *> families,
                                  int weight = 400,
                                  FontSlant slant = FontSlant::NORMAL)
{
    for (const char *family : families) {
        const FontFace *best = nullptr;
        int bestScore = std::numeric_limits<int>::max();
        for (const FontFace &face : installed) {
            if (!familyNameEquals(face.family(), family)) continue;
            const int slantPenalty = face.slant() == slant ? 0 : 10000;
            const int score = slantPenalty + std::abs(face.weight() - weight);
            if (score < bestScore) {
                best = &face;
                bestScore = score;
            }
        }
        if (best != nullptr) return best;
    }
    return nullptr;
}

FontFace aliasFace(const FontFace &source, const char *alias, int weight,
                   std::initializer_list<FontCodepointRange> ranges = {})
{
    FontFace face = FontFace::fromFile(FontDescriptor(alias, weight), source.path(), source.faceIndex());
    for (const FontCodepointRange &range : ranges) {
        face.addCodepointRange(range.first, range.last);
    }
    return face;
}

} // namespace

namespace {

struct DiscoveryCache
{
    std::mutex mutex;
    std::optional<std::vector<FontFace>> value;
    std::uint64_t generation = 0;
};

struct DefaultsCache
{
    std::mutex mutex;
    std::optional<std::vector<FontFace>> value;
};

DiscoveryCache &discoveryCache()
{
    static DiscoveryCache cache;
    return cache;
}

DefaultsCache &defaultsCache()
{
    static DefaultsCache cache;
    return cache;
}

} // namespace

std::vector<FontFace> FontSystem::discoverInstalledFontFaces()
{
    {
        DiscoveryCache &cache = discoveryCache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        if (cache.value.has_value()) return *cache.value;
    }

    const std::vector<detail::DiscoveredFontFace> raw = detail::discoverInstalledFontFaces();
    std::vector<FontFace> built;
    built.reserve(raw.size());
    std::unordered_set<std::string> seen;
    for (const detail::DiscoveredFontFace &face : raw) {
        const std::string key = face.family + "\x1f" + face.path + "\x1f"
            + std::to_string(face.faceIndex) + "\x1f" + std::to_string(face.weight)
            + (face.italic ? "\x1f" "1" : "\x1f" "0");
        if (!seen.insert(key).second) continue;
        FontDescriptor descriptor(face.family, face.weight,
                                  face.italic ? FontSlant::ITALIC : FontSlant::NORMAL);
        built.push_back(FontFace::fromFile(descriptor, face.path, face.faceIndex));
    }

    DiscoveryCache &cache = discoveryCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    if (!cache.value.has_value()) {
        cache.value = std::move(built);
        ++cache.generation;
        if (cache.generation == 0) cache.generation = 1;
    }
    return *cache.value;
}

std::vector<FontFace> FontSystem::defaultSystemFontFaces()
{
    {
        DefaultsCache &cache = defaultsCache();
        std::lock_guard<std::mutex> lock(cache.mutex);
        if (cache.value.has_value()) return *cache.value;
    }

    std::vector<FontFace> built = [] {
        const std::vector<FontFace> installed = discoverInstalledFontFaces();
        std::vector<FontFace> faces;
        if (installed.empty()) return faces;

#if defined(_WIN32)
        const auto primaryFamilies = {"Segoe UI", "Arial"};
        const auto cjkFamilies = {"Microsoft YaHei", "Microsoft JhengHei", "Yu Gothic", "Malgun Gothic"};
        const auto arabicFamilies = {"Arial", "Segoe UI"};
        const auto hebrewFamilies = {"Arial", "Segoe UI"};
        const auto symbolFamilies = {"Segoe UI Symbol", "Segoe UI Emoji"};
        const auto serifFamilies = {"Georgia", "Times New Roman"};
        const auto monoFamilies = {"Consolas", "Courier New"};
#elif defined(__APPLE__)
        const auto primaryFamilies = {".AppleSystemUIFont", "SF Pro", "SF Pro Text", "Helvetica Neue", "Helvetica"};
        const auto cjkFamilies = {"PingFang SC", "PingFang TC", "Hiragino Sans"};
        const auto arabicFamilies = {"SF Arabic", "Geeza Pro", "Arial"};
        const auto hebrewFamilies = {"SF Hebrew", "Arial Hebrew", "Arial"};
        const auto symbolFamilies = {"Apple Symbols", "Apple Color Emoji"};
        const auto serifFamilies = {"Georgia", "Times"};
        const auto monoFamilies = {"Menlo", "SF Mono", "Monaco"};
#else
        const auto primaryFamilies = {"DejaVu Sans", "Noto Sans", "Liberation Sans",
                          "Ubuntu", "Cantarell", "Roboto", "FreeSans"};
        const auto cjkFamilies = {"Noto Sans CJK SC", "Noto Sans CJK JP", "Noto Sans CJK TC",
                      "Noto Sans CJK KR", "Noto Sans SC", "Noto Sans TC",
                      "Noto Sans JP", "Noto Sans KR", "Source Han Sans SC",
                      "Source Han Sans TC", "Source Han Sans JP", "Source Han Sans KR",
                      "WenQuanYi Zen Hei", "WenQuanYi Micro Hei"};
        const auto arabicFamilies = {"Noto Naskh Arabic", "Noto Sans Arabic", "Amiri",
                         "Scheherazade New", "DejaVu Sans"};
        const auto hebrewFamilies = {"Noto Sans Hebrew", "DejaVu Sans", "Liberation Sans"};
        const auto symbolFamilies = {"Noto Sans Symbols", "Noto Sans Symbols 2",
                         "Noto Color Emoji", "Symbola", "DejaVu Sans"};
        const auto serifFamilies = {"DejaVu Serif", "Noto Serif", "Liberation Serif", "FreeSerif"};
        const auto monoFamilies = {"DejaVu Sans Mono", "Noto Sans Mono", "Liberation Mono",
                       "Ubuntu Mono", "Roboto Mono", "FreeMono"};
#endif

        const auto addAlias = [&](const char *alias, const auto &families, int weight,
                                  std::initializer_list<FontCodepointRange> ranges = {}) {
            if (const FontFace *source = findPreferredFace(installed, families, weight)) {
                faces.push_back(aliasFace(*source, alias, weight, ranges));
            }
        };

        const auto latinRanges = {FontCodepointRange(0x0000, 0x024F), FontCodepointRange(0x2000, 0x206F)};
        addAlias(kDefaultPrimaryFamily, primaryFamilies, 400, latinRanges);
        addAlias(kDefaultPrimaryFamily, primaryFamilies, 600, latinRanges);
        addAlias(kDefaultPrimaryFamily, primaryFamilies, 700, latinRanges);
        addAlias(kDefaultCjkFamily, cjkFamilies, 400,
                 {FontCodepointRange(0x3000, 0x30FF), FontCodepointRange(0x3400, 0x9FFF),
                  FontCodepointRange(0xF900, 0xFAFF), FontCodepointRange(0xFF00, 0xFFEF)});
        addAlias(kDefaultArabicFamily, arabicFamilies, 400,
                 {FontCodepointRange(0x0590, 0x05FF), FontCodepointRange(0x0600, 0x06FF),
                  FontCodepointRange(0x0750, 0x077F), FontCodepointRange(0x08A0, 0x08FF)});
        addAlias(kDefaultHebrewFamily, hebrewFamilies, 400,
                 {FontCodepointRange(0x0590, 0x05FF)});
        addAlias(kDefaultSymbolFamily, symbolFamilies, 400,
                 {FontCodepointRange(0x2000, 0x27BF),
                  FontCodepointRange(0x2B00, 0x2BFF),
                  FontCodepointRange(0x1F000, 0x1FAFF),
                  FontCodepointRange(0xFE00, 0xFE0F)});
        addAlias(kDefaultSerifFamily, serifFamilies, 400);
        addAlias(kDefaultMonoFamily, monoFamilies, 400);
        return faces;
    }();

    DefaultsCache &cache = defaultsCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    if (!cache.value.has_value()) cache.value = std::move(built);
    return *cache.value;
}

void FontSystem::refreshDefaultSystemFontFaces()
{
    refreshDiscoveredFontFaces();
    refreshDefaultSystemFontFacesOnly();
}

void FontSystem::refreshDiscoveredFontFaces()
{
    std::lock_guard<std::mutex> lock(discoveryCache().mutex);
    discoveryCache().value.reset();
}

void FontSystem::refreshDefaultSystemFontFacesOnly()
{
    std::lock_guard<std::mutex> lock(defaultsCache().mutex);
    defaultsCache().value.reset();
}

std::uint64_t FontSystem::refreshInstalledFonts()
{
    refreshDiscoveredFontFaces();
    (void)discoverInstalledFontFaces();
    refreshDefaultSystemFontFacesOnly();
    return installedFontGeneration();
}

std::uint64_t FontSystem::installedFontGeneration()
{
    DiscoveryCache &cache = discoveryCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    return cache.generation;
}

} // namespace wsc
