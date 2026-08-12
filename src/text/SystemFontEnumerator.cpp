// Cross-platform system font enumeration. Uses the OS's native font manager
// so consumers can find installed fonts by their real family names (e.g.
// "Menlo" on macOS, "Consolas" on Windows, "DejaVu Sans" on Linux) instead
// of relying on the hardcoded fallback paths baked into FontSystem.

#include "text/SystemFontEnumerator.h"

#include "wsc/Font.h"

#include <algorithm>
#include <cstring>
#include <string>
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
#elif defined(__linux__)
#  if __has_include(<fontconfig/fontconfig.h>)
#    define WHATSCANVAS_HAS_FONTCONFIG 1
#    include <fontconfig/fontconfig.h>
#  endif
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

std::vector<DiscoveredFontFace> discoverApple()
{
    std::vector<DiscoveredFontFace> out;

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
            CFRelease(urlRef);
            if (path.empty()) continue;

            DiscoveredFontFace face;
            face.family = cfStringToUtf8(familyName);
            face.path = std::move(path);
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
    return out;
}

#elif defined(_WIN32)

std::string wideToUtf8(const std::wstring &w)
{
    if (w.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
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
    if (FAILED(factory->GetSystemFontCollection(systemCollection.GetAddressOf(), FALSE))) {
        return out;
    }

    const UINT32 familyCount = systemCollection->GetFontFamilyCount();
    for (UINT32 i = 0; i < familyCount; ++i) {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(systemCollection->GetFontFamily(i, family.GetAddressOf()))) continue;

        ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(family->GetFamilyNames(names.GetAddressOf()))) continue;

        UINT32 nameIndex = 0;
        BOOL exists = FALSE;
        names->FindLocaleName(L"en-us", &nameIndex, &exists);
        if (!exists) nameIndex = 0;

        UINT32 nameLength = 0;
        if (FAILED(names->GetStringLength(nameIndex, &nameLength))) continue;
        std::wstring familyNameW(nameLength + 1, L'\0');
        if (FAILED(names->GetString(nameIndex, familyNameW.data(), nameLength + 1))) continue;
        familyNameW.resize(nameLength);

        const std::string familyName = wideToUtf8(familyNameW);
        const UINT32 fontCount = family->GetFontCount();
        for (UINT32 f = 0; f < fontCount; ++f) {
            ComPtr<IDWriteFont> font;
            if (FAILED(family->GetFont(f, font.GetAddressOf()))) continue;

            ComPtr<IDWriteFontFace> face;
            if (FAILED(font->CreateFontFace(face.GetAddressOf()))) continue;

            UINT32 fileCount = 0;
            face->GetFiles(&fileCount, nullptr);
            if (fileCount == 0) continue;
            std::vector<IDWriteFontFile *> files(fileCount, nullptr);
            if (FAILED(face->GetFiles(&fileCount, files.data()))) continue;

            IDWriteFontFile *file = files[0];
            for (UINT32 k = 1; k < fileCount; ++k) if (files[k]) files[k]->Release();

            const void *refKey = nullptr;
            UINT32 refKeySize = 0;
            file->GetReferenceKey(&refKey, &refKeySize);

            ComPtr<IDWriteFontFileLoader> loader;
            file->GetLoader(loader.GetAddressOf());
            ComPtr<IDWriteLocalFontFileLoader> localLoader;
            if (SUCCEEDED(loader.As(&localLoader))) {
                UINT32 pathLen = 0;
                if (SUCCEEDED(localLoader->GetFilePathLengthFromKey(refKey, refKeySize, &pathLen))
                    && pathLen > 0) {
                    std::wstring pathW(pathLen + 1, L'\0');
                    if (SUCCEEDED(localLoader->GetFilePathFromKey(refKey, refKeySize,
                                                                  pathW.data(), pathLen + 1))) {
                        pathW.resize(pathLen);
                        DiscoveredFontFace fd;
                        fd.family = familyName;
                        fd.path = wideToUtf8(pathW);
                        fd.faceIndex = static_cast<int>(face->GetIndex());
                        fd.weight = static_cast<int>(font->GetWeight());
                        fd.italic = font->GetStyle() != DWRITE_FONT_STYLE_NORMAL;
                        if (!fd.family.empty() && !fd.path.empty()) {
                            out.push_back(std::move(fd));
                        }
                    }
                }
            }
            file->Release();
        }
    }
    return out;
}

#elif defined(__linux__) && defined(WHATSCANVAS_HAS_FONTCONFIG)

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
            FcChar8 *familyStr = nullptr;
            FcChar8 *fileStr = nullptr;
            int faceIndex = 0;
            int weight = FC_WEIGHT_REGULAR;
            int slant = FC_SLANT_ROMAN;
            if (FcPatternGetString(entry, FC_FAMILY, 0, &familyStr) != FcResultMatch) continue;
            if (FcPatternGetString(entry, FC_FILE, 0, &fileStr) != FcResultMatch) continue;
            FcPatternGetInteger(entry, FC_INDEX, 0, &faceIndex);
            FcPatternGetInteger(entry, FC_WEIGHT, 0, &weight);
            FcPatternGetInteger(entry, FC_SLANT, 0, &slant);

            DiscoveredFontFace fd;
            fd.family = reinterpret_cast<const char *>(familyStr);
            fd.path = reinterpret_cast<const char *>(fileStr);
            fd.faceIndex = faceIndex;
            fd.italic = slant != FC_SLANT_ROMAN;
            // fontconfig weight is 0..215; map to CSS 100..900 by rule of thumb.
            if (weight <= FC_WEIGHT_THIN) fd.weight = 100;
            else if (weight <= FC_WEIGHT_EXTRALIGHT) fd.weight = 200;
            else if (weight <= FC_WEIGHT_LIGHT) fd.weight = 300;
            else if (weight <= FC_WEIGHT_REGULAR) fd.weight = 400;
            else if (weight <= FC_WEIGHT_MEDIUM) fd.weight = 500;
            else if (weight <= FC_WEIGHT_SEMIBOLD) fd.weight = 600;
            else if (weight <= FC_WEIGHT_BOLD) fd.weight = 700;
            else if (weight <= FC_WEIGHT_EXTRABOLD) fd.weight = 800;
            else fd.weight = 900;
            if (!fd.family.empty() && !fd.path.empty()) {
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

std::vector<FontFace> FontSystem::discoverInstalledFontFaces()
{
    const std::vector<detail::DiscoveredFontFace> raw = detail::discoverInstalledFontFaces();
    std::vector<FontFace> out;
    out.reserve(raw.size());
    for (const detail::DiscoveredFontFace &face : raw) {
        FontDescriptor descriptor(face.family, face.weight,
                                  face.italic ? FontSlant::ITALIC : FontSlant::NORMAL);
        out.push_back(FontFace::fromFile(descriptor, face.path, face.faceIndex));
    }
    return out;
}

} // namespace wsc
