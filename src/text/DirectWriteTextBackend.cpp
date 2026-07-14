#include "text/DirectWriteTextBackend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// DirectWrite headers
#include <dwrite.h>
#include <dwrite_1.h>
#include <dwrite_3.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wincodec.h>
#include <objbase.h>
// Safe-release helper
template <typename T>
inline void safeRelease(T *&p)
{
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}
#endif

#include "canvas/Paint.h"
#include "text/ITextBackend.h"
#include "text/TextUtils.h"

namespace wsc::text {

bool isDirectWriteAvailable()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

#ifdef _WIN32

namespace {

// Upper bound (in pixels) on either dimension of a rasterized text bitmap.
// User-controlled inputs (font size, letterSpacing) feed into the layout
// metrics; without a clamp a hostile input can request an enormous bitmap and
// trigger a huge/overflowing allocation (DoS). At this bound the worst-case
// buffer is 16384 * 16384 * 4 == 1 GiB, which stays within size_t on 32-bit.
constexpr int kMaxBitmapDimension = 16384;

// Convert UTF-8 to UTF-16 wstring (same logic as NativeText.cpp).
std::wstring toWideString(const std::string &value)
{
    if (value.empty()) {
        return {};
    }
    int codePage = CP_UTF8;
    int flags = MB_ERR_INVALID_CHARS;
    int count = MultiByteToWideChar(codePage, flags, value.c_str(), -1, nullptr, 0);
    if (count <= 0) {
        codePage = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(codePage, flags, value.c_str(), -1, nullptr, 0);
    }
    if (count <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(count), L'\0');
    if (MultiByteToWideChar(codePage, flags, value.c_str(), -1, wide.data(), count) <= 0) {
        return {};
    }
    if (!wide.empty() && wide.back() == L'\0') {
        wide.pop_back();
    }
    return wide;
}

// Map WhatsCanvas FontSlant to DWRITE_FONT_STYLE.
DWRITE_FONT_STYLE mapFontSlant(FontSlant slant)
{
    switch (slant) {
    case FontSlant::ITALIC:
        return DWRITE_FONT_STYLE_ITALIC;
    case FontSlant::OBLIQUE:
        return DWRITE_FONT_STYLE_OBLIQUE;
    default:
        return DWRITE_FONT_STYLE_NORMAL;
    }
}

// Map WhatsCanvas font weight (CSS-style 100..900) to DWRITE_FONT_WEIGHT.
DWRITE_FONT_WEIGHT mapFontWeight(int weight)
{
    weight = std::max(100, std::min(900, weight));
    return static_cast<DWRITE_FONT_WEIGHT>(weight);
}

// Convert a DWRITE_MATRIX to a D2D1::Matrix3x2F is not needed; we use
// IDWriteTextLayout directly.

// -----------------------------------------------------------------------
// Custom font collection (registerFontFace with on-disk font files)
// -----------------------------------------------------------------------

// Key passed to CreateCustomFontCollection. `sources` points at the backend's
// live source list; `generation` busts DirectWrite's (loader,key) collection
// cache so a rebuild after registering a new font produces a fresh collection.
struct CustomFontSource
{
    std::wstring path;                               // FILE-based (empty for memory).
    std::shared_ptr<std::vector<std::uint8_t>> data; // MEMORY-based (null for file).
};

struct CustomFontCollectionKey
{
    const std::vector<CustomFontSource> *sources = nullptr;
    IDWriteInMemoryFontFileLoader *memoryLoader = nullptr;
    unsigned int generation = 0;
};

// Enumerates on-disk and in-memory font files for a custom collection.
class CustomFontFileEnumerator final : public IDWriteFontFileEnumerator
{
public:
    CustomFontFileEnumerator(IDWriteFactory *factory, IDWriteInMemoryFontFileLoader *memoryLoader,
                             std::vector<CustomFontSource> sources)
        : factory_(factory), memoryLoader_(memoryLoader), sources_(std::move(sources))
    {
        if (factory_ != nullptr) {
            factory_->AddRef();
        }
        if (memoryLoader_ != nullptr) {
            memoryLoader_->AddRef();
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **ppv) override
    {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteFontFileEnumerator)) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&refCount_));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG count = InterlockedDecrement(&refCount_);
        if (count == 0) {
            delete this;
        }
        return static_cast<ULONG>(count);
    }

    HRESULT STDMETHODCALLTYPE MoveNext(BOOL *hasCurrentFile) override
    {
        if (hasCurrentFile == nullptr) {
            return E_POINTER;
        }
        *hasCurrentFile = FALSE;
        safeRelease(currentFile_);
        if (index_ + 1 >= static_cast<int>(sources_.size())) {
            return S_OK;
        }
        ++index_;
        const CustomFontSource &source = sources_[index_];
        IDWriteFontFile *file = nullptr;
        HRESULT hr = E_FAIL;
        if (source.data != nullptr && !source.data->empty()) {
            if (memoryLoader_ == nullptr) {
                return E_FAIL;
            }
            hr = memoryLoader_->CreateInMemoryFontFileReference(
                factory_, source.data->data(), static_cast<UINT32>(source.data->size()), nullptr, &file);
        } else {
            hr = factory_->CreateFontFileReference(source.path.c_str(), nullptr, &file);
        }
        if (FAILED(hr) || file == nullptr) {
            return hr;
        }
        currentFile_ = file;
        *hasCurrentFile = TRUE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile **fontFile) override
    {
        if (fontFile == nullptr) {
            return E_POINTER;
        }
        *fontFile = currentFile_;
        if (currentFile_ != nullptr) {
            currentFile_->AddRef();
            return S_OK;
        }
        return E_FAIL;
    }

private:
    ~CustomFontFileEnumerator()
    {
        safeRelease(currentFile_);
        if (memoryLoader_ != nullptr) {
            memoryLoader_->Release();
        }
        if (factory_ != nullptr) {
            factory_->Release();
        }
    }

    LONG refCount_ = 1;
    IDWriteFactory *factory_ = nullptr;
    IDWriteInMemoryFontFileLoader *memoryLoader_ = nullptr;
    std::vector<CustomFontSource> sources_;
    int index_ = -1;
    IDWriteFontFile *currentFile_ = nullptr;
};

// Collection loader that builds enumerators from a CustomFontCollectionKey.
class CustomFontCollectionLoader final : public IDWriteFontCollectionLoader
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **ppv) override
    {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteFontCollectionLoader)) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&refCount_));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const LONG count = InterlockedDecrement(&refCount_);
        if (count == 0) {
            delete this;
        }
        return static_cast<ULONG>(count);
    }

    HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory *factory, const void *collectionKey,
                                                      UINT32 collectionKeySize,
                                                      IDWriteFontFileEnumerator **enumerator) override
    {
        if (enumerator == nullptr) {
            return E_POINTER;
        }
        *enumerator = nullptr;
        if (collectionKey == nullptr || collectionKeySize != sizeof(CustomFontCollectionKey)) {
            return E_INVALIDARG;
        }
        const auto *key = static_cast<const CustomFontCollectionKey *>(collectionKey);
        std::vector<CustomFontSource> sources =
            key->sources != nullptr ? *key->sources : std::vector<CustomFontSource>{};
        *enumerator = new CustomFontFileEnumerator(factory, key->memoryLoader, std::move(sources));
        return S_OK;
    }

private:
    ~CustomFontCollectionLoader() = default;
    LONG refCount_ = 1;
};

// -----------------------------------------------------------------------
// DirectWriteTextBackend
// -----------------------------------------------------------------------

class DirectWriteTextBackend final : public ITextBackend
{
public:
    explicit DirectWriteTextBackend(DirectWriteBackendOptions options)
        : options_(options)
    {
        HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown **>(&dwriteFactory_));
        if (FAILED(hr) || dwriteFactory_ == nullptr) {
            return;
        }
        // IDWriteTextLayout applies the system font fallback resolver
        // automatically for characters not covered by the requested family, so
        // no explicit IDWriteFontFallback wiring is required here.
        dwriteFactory_->GetSystemFontCollection(&systemFontCollection_, FALSE);
        available_ = true;
    }

    ~DirectWriteTextBackend() override
    {
        safeRelease(customFontCollection_);
        safeRelease(systemFontCollection_);
        safeRelease(customFontFallback_);
        if (collectionLoader_ != nullptr) {
            if (dwriteFactory_ != nullptr) {
                dwriteFactory_->UnregisterFontCollectionLoader(collectionLoader_);
            }
            collectionLoader_->Release();
            collectionLoader_ = nullptr;
        }
        if (memoryFontLoader_ != nullptr) {
            if (dwriteFactory_ != nullptr) {
                dwriteFactory_->UnregisterFontFileLoader(memoryFontLoader_);
            }
            memoryFontLoader_->Release();
            memoryFontLoader_ = nullptr;
        }
        safeRelease(dwriteFactory_);
    }

    bool isAvailable() const { return available_; }

    // -- ITextBackend --------------------------------------------------------

    bool registerFontFace(const FontFace &face) override
    {
        // Register an on-disk or in-memory font so its family resolves by name.
        if (!available_ || !face.isValid()) {
            return false;
        }
        if (face.sourceType() == wsc::FontSourceType::FILE) {
            CustomFontSource source;
            source.path = toWideString(face.path());
            customFontSources_.push_back(std::move(source));
            return rebuildCustomFontCollection();
        }
        if (face.sourceType() == wsc::FontSourceType::MEMORY && face.bytes() != nullptr) {
            if (!ensureMemoryFontLoader()) {
                return false; // In-memory loader needs IDWriteFactory5 (Windows 10+).
            }
            CustomFontSource source;
            source.data = std::make_shared<std::vector<std::uint8_t>>(*face.bytes());
            customFontSources_.push_back(std::move(source));
            return rebuildCustomFontCollection();
        }
        return false;
    }

    bool setFontFallbackChain(const FontFallbackChain &chain) override
    {
        // Build a custom DirectWrite fallback: map the whole Unicode range to the
        // requested families (in order), then append the system fallback so any
        // uncovered scripts still resolve. Applied to layouts via IDWriteTextLayout2.
        if (!available_ || dwriteFactory_ == nullptr) {
            return false;
        }
        const std::vector<std::string> families = chain.familiesInResolutionOrder();
        if (families.empty()) {
            safeRelease(customFontFallback_);
            return true; // Cleared.
        }
        IDWriteFactory2 *factory2 = nullptr;
        if (FAILED(dwriteFactory_->QueryInterface(__uuidof(IDWriteFactory2),
                                                  reinterpret_cast<void **>(&factory2)))
            || factory2 == nullptr) {
            return false;
        }
        IDWriteFontFallbackBuilder *builder = nullptr;
        HRESULT hr = factory2->CreateFontFallbackBuilder(&builder);
        if (FAILED(hr) || builder == nullptr) {
            factory2->Release();
            return false;
        }

        std::vector<std::wstring> wide;
        wide.reserve(families.size());
        for (const std::string &family : families) {
            wide.push_back(toWideString(family));
        }
        std::vector<const WCHAR *> names;
        names.reserve(wide.size());
        for (const std::wstring &w : wide) {
            names.push_back(w.c_str());
        }

        DWRITE_UNICODE_RANGE range{0x0, 0x10FFFF};
        hr = builder->AddMapping(&range, 1, names.data(), static_cast<UINT32>(names.size()), nullptr,
                                 nullptr, nullptr, 1.0f);

        IDWriteFontFallback *systemFallback = nullptr;
        if (SUCCEEDED(factory2->GetSystemFontFallback(&systemFallback)) && systemFallback != nullptr) {
            builder->AddMappings(systemFallback);
            systemFallback->Release();
        }

        IDWriteFontFallback *fallback = nullptr;
        if (SUCCEEDED(hr)) {
            hr = builder->CreateFontFallback(&fallback);
        }
        builder->Release();
        factory2->Release();
        if (FAILED(hr) || fallback == nullptr) {
            return false;
        }
        safeRelease(customFontFallback_);
        customFontFallback_ = fallback;
        return true;
    }

    std::vector<std::string> resolveFontFamilies(const std::string &preferredFamily) const override
    {
        return {preferredFamily};
    }

    std::vector<TextLineBreak> breakLines(const std::string &text, float maxWidth,
                                          const Paint &paint) const override
    {
        std::vector<TextLineBreak> breaks;
        if (text.empty() || maxWidth <= 0.0f || !available_) {
            return breaks;
        }

        const std::string normalized = wsc::text::normalizeUtf8ForText(text);
        if (normalized.empty()) {
            return breaks;
        }

        // Greedy word wrap on ASCII/whitespace boundaries using DirectWrite
        // measurement. Offsets are byte offsets into the normalized UTF-8 text.
        std::size_t lineStart = 0;
        std::size_t lastBreak = std::string::npos; // last whitespace position
        std::size_t i = 0;
        while (i <= normalized.size()) {
            const bool atEnd = (i == normalized.size());
            const char c = atEnd ? '\0' : normalized[i];
            if (atEnd || c == '\n') {
                TextLineBreak lb;
                lb.sourceStart = lineStart;
                lb.sourceLength = i - lineStart;
                lb.width = measureTextWidth(normalized.substr(lineStart, i - lineStart), paint);
                breaks.push_back(lb);
                lineStart = i + 1;
                lastBreak = std::string::npos;
                ++i;
                continue;
            }
            if (c == ' ' || c == '\t') {
                lastBreak = i;
            }
            const std::string candidate = normalized.substr(lineStart, i - lineStart + 1);
            const float w = measureTextWidth(candidate, paint);
            if (w > maxWidth && i > lineStart) {
                const std::size_t breakAt = (lastBreak != std::string::npos && lastBreak > lineStart)
                                                ? lastBreak
                                                : i;
                TextLineBreak lb;
                lb.sourceStart = lineStart;
                lb.sourceLength = breakAt - lineStart;
                lb.width = measureTextWidth(normalized.substr(lineStart, breakAt - lineStart), paint);
                breaks.push_back(lb);
                lineStart = (breakAt == i) ? i : breakAt + 1;
                lastBreak = std::string::npos;
                i = lineStart;
                continue;
            }
            ++i;
        }
        return breaks;
    }

    bool hasGlyphForCodepoint(std::uint32_t codepoint, const Paint &paint) const override
    {
        if (!available_ || codepoint < 32) {
            return false;
        }
        // Check the requested (or default) family first.
        const std::wstring family = toWideString(paint.hasFontFamily() ? paint.getFontFamily()
                                                                        : std::string("Segoe UI"));
        IDWriteFontCollection *fontCollection = chooseFontCollection(family);
        if (fontCollection != nullptr) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            fontCollection->FindFamilyName(family.c_str(), &index, &exists);
            if (exists) {
                ComPtr<IDWriteFontFamily> fontFamily;
                fontCollection->GetFontFamily(index, &fontFamily);
                if (fontFamily) {
                    ComPtr<IDWriteFont> font;
                    fontFamily->GetFirstMatchingFont(mapFontWeight(paint.getFontWeight()),
                                                     DWRITE_FONT_STRETCH_NORMAL,
                                                     mapFontSlant(paint.getFontSlant()), &font);
                    if (font) {
                        BOOL hasChar = FALSE;
                        if (SUCCEEDED(font->HasCharacter(codepoint, &hasChar)) && hasChar) {
                            return true;
                        }
                    }
                }
            }
        }
        // The requested family does not cover it, but DirectWrite's system font
        // fallback resolver (applied automatically by IDWriteTextLayout) covers
        // essentially all assigned Unicode codepoints, so report true.
        return true;
    }

    std::vector<TextBackendDiagnostic> diagnostics() const override
    {
        return diagnostics_;
    }

    float measureTextWidth(const std::string &text, const Paint &paint) const override
    {
        if (!available_ || text.empty() || paint.getTextSize() <= 0.0f) {
            return 0.0f;
        }
        const std::wstring wide = toWideString(text);
        if (wide.empty()) {
            return 0.0f;
        }
        ComPtr<IDWriteTextLayout> layout = createLayout(wide, paint, 0.0f);
        if (layout == nullptr) {
            return 0.0f;
        }
        DWRITE_TEXT_METRICS metrics;
        layout->GetMetrics(&metrics);
        // Letter spacing is baked into the layout, so the metrics already include it.
        return metrics.width;
    }

    RectF measureTextBounds(const std::string &text, const Paint &paint) const override
    {
        if (!available_ || text.empty() || paint.getTextSize() <= 0.0f) {
            return {};
        }
        const std::wstring wide = toWideString(text);
        if (wide.empty()) {
            return {};
        }
        ComPtr<IDWriteTextLayout> layout = createLayout(wide, paint, 0.0f);
        if (layout == nullptr) {
            return {};
        }
        DWRITE_TEXT_METRICS metrics;
        layout->GetMetrics(&metrics);
        return {0.0f, 0.0f, metrics.width, metrics.height};
    }

    TextMetrics measureTextMetrics(const std::string &text, const Paint &paint) const override
    {
        TextMetrics m;
        if (!available_ || text.empty() || paint.getTextSize() <= 0.0f) {
            return m;
        }
        const std::wstring wide = toWideString(text);
        if (wide.empty()) {
            return m;
        }
        ComPtr<IDWriteTextLayout> layout = createLayout(wide, paint, 0.0f);
        if (layout == nullptr) {
            return m;
        }
        DWRITE_TEXT_METRICS metrics;
        layout->GetMetrics(&metrics);
        m.width = metrics.width;
        m.height = metrics.height;
        m.bounds = {0.0f, 0.0f, metrics.width, metrics.height};

        // Get font metrics for ascent/descent/lineGap.
        const std::wstring family = toWideString(paint.hasFontFamily() ? paint.getFontFamily()
                                                                        : "Segoe UI");
        IDWriteFontCollection *fontCollection = chooseFontCollection(family);
        if (fontCollection != nullptr) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            fontCollection->FindFamilyName(family.c_str(), &index, &exists);
            if (exists) {
                ComPtr<IDWriteFontFamily> fontFamily;
                fontCollection->GetFontFamily(index, &fontFamily);
                if (fontFamily != nullptr) {
                    ComPtr<IDWriteFont> font;
                    fontFamily->GetFirstMatchingFont(mapFontWeight(paint.getFontWeight()),
                                                     DWRITE_FONT_STRETCH_NORMAL,
                                                     mapFontSlant(paint.getFontSlant()), &font);
                    if (font != nullptr) {
                        DWRITE_FONT_METRICS fontMetrics;
                        font->GetMetrics(&fontMetrics);
                        const float size = paint.getTextSize();
                        const float designUnitsPerEm = static_cast<float>(fontMetrics.designUnitsPerEm);
                        if (designUnitsPerEm > 0.0f) {
                            m.ascent = static_cast<float>(fontMetrics.ascent) / designUnitsPerEm * size;
                            m.descent = static_cast<float>(fontMetrics.descent) / designUnitsPerEm * size;
                            m.lineGap = static_cast<float>(fontMetrics.lineGap) / designUnitsPerEm * size;
                            m.lineHeight = m.ascent + m.descent + m.lineGap;
                            m.top = 0.0f;
                            m.bottom = m.lineHeight;
                        }
                    }
                }
            }
        }
        return m;
    }

    TextRenderResult renderText(const std::string &text, float x, float y,
                                const Paint &paint) const override
    {
        TextRenderResult result;
        if (!available_ || text.empty() || paint.getTextSize() <= 0.0f) {
            return result;
        }

        const std::string normalized = wsc::text::normalizeUtf8ForText(text);
        if (normalized.empty()) {
            return result;
        }

        const std::wstring wide = toWideString(normalized);
        if (wide.empty()) {
            return result;
        }

        ComPtr<IDWriteTextLayout> layout = createLayout(wide, paint, 0.0f);
        if (layout == nullptr) {
            return result;
        }

        DWRITE_TEXT_METRICS metrics;
        layout->GetMetrics(&metrics);

        // Letter spacing is baked into the layout, so metrics already include it.
        const float totalWidth = metrics.width;
        const float totalHeight = metrics.height;

        float alignedX = x;
        if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
            alignedX -= totalWidth * 0.5f;
        } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
            alignedX -= totalWidth;
        }

        // Clamp against pathological (user-controlled) dimensions. Work in
        // double and bound BEFORE the int cast so a huge float can't overflow
        // the cast, then bail out gracefully if the request is oversized.
        if (!std::isfinite(totalWidth) || !std::isfinite(totalHeight)) {
            return result;
        }
        const double ceilWidth = std::ceil(static_cast<double>(totalWidth));
        const double ceilHeight = std::ceil(static_cast<double>(totalHeight));
        if (ceilWidth > static_cast<double>(kMaxBitmapDimension)
            || ceilHeight > static_cast<double>(kMaxBitmapDimension)) {
            return result;
        }
        const int pixelWidth = std::max(1, static_cast<int>(ceilWidth));
        const int pixelHeight = std::max(1, static_cast<int>(ceilHeight));
        std::vector<unsigned char> pixels = renderLayoutToBitmap(layout.Get(), pixelWidth, pixelHeight, paint);
        if (pixels.empty()) {
            return result;
        }

        result.kind = TextRenderKind::Bitmap;
        result.drawX = alignedX;
        result.drawY = y + wsc::text::textBaselineOffset(paint.getTextBaseline(), totalHeight);
        result.width = totalWidth;
        result.height = totalHeight;
        result.bitmapWidth = pixelWidth;
        result.bitmapHeight = pixelHeight;
        result.bitmapPixels = std::move(pixels);
        return result;
    }

private:
    // Simple COM smart pointer to avoid ATL/WTL dependency.
    template <typename T>
    class ComPtr
    {
    public:
        ComPtr() = default;
        explicit ComPtr(T *p) : ptr_(p) {}
        ComPtr(std::nullptr_t) {}
        ~ComPtr() { safeRelease(ptr_); }
        ComPtr(const ComPtr &) = delete;
        ComPtr &operator=(const ComPtr &) = delete;
        ComPtr(ComPtr &&other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
        ComPtr &operator=(ComPtr &&other) noexcept
        {
            if (this != &other) {
                safeRelease(ptr_);
                ptr_ = other.ptr_;
                other.ptr_ = nullptr;
            }
            return *this;
        }
        T *Get() const { return ptr_; }
        T **operator&() { return &ptr_; }
        T *operator->() const { return ptr_; }
        explicit operator bool() const { return ptr_ != nullptr; }
        bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
        bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

    private:
        T *ptr_ = nullptr;
    };

    // Rebuild the custom font collection from the registered fonts. The
    // generation counter busts DirectWrite's (loader,key) collection cache.
    bool rebuildCustomFontCollection()
    {
        if (dwriteFactory_ == nullptr) {
            return false;
        }
        if (collectionLoader_ == nullptr) {
            collectionLoader_ = new CustomFontCollectionLoader();
            if (FAILED(dwriteFactory_->RegisterFontCollectionLoader(collectionLoader_))) {
                collectionLoader_->Release();
                collectionLoader_ = nullptr;
                return false;
            }
        }
        safeRelease(customFontCollection_);
        ++collectionGeneration_;
        CustomFontCollectionKey key{&customFontSources_, memoryFontLoader_, collectionGeneration_};
        IDWriteFontCollection *collection = nullptr;
        const HRESULT hr = dwriteFactory_->CreateCustomFontCollection(collectionLoader_, &key,
                                                                      sizeof(key), &collection);
        if (FAILED(hr) || collection == nullptr) {
            return false;
        }
        customFontCollection_ = collection;
        return true;
    }

    // Lazily create + register an in-memory font file loader (IDWriteFactory5,
    // Windows 10+). Returns false when unavailable.
    bool ensureMemoryFontLoader()
    {
        if (memoryFontLoader_ != nullptr) {
            return true;
        }
        if (dwriteFactory_ == nullptr) {
            return false;
        }
        IDWriteFactory5 *factory5 = nullptr;
        if (FAILED(dwriteFactory_->QueryInterface(__uuidof(IDWriteFactory5),
                                                  reinterpret_cast<void **>(&factory5)))
            || factory5 == nullptr) {
            return false;
        }
        IDWriteInMemoryFontFileLoader *loader = nullptr;
        HRESULT hr = factory5->CreateInMemoryFontFileLoader(&loader);
        if (SUCCEEDED(hr) && loader != nullptr) {
            hr = factory5->RegisterFontFileLoader(loader);
        }
        factory5->Release();
        if (FAILED(hr) || loader == nullptr) {
            safeRelease(loader);
            return false;
        }
        memoryFontLoader_ = loader;
        return true;
    }

    // Prefer a registered custom font whose family matches, else the system font
    // collection (which also drives automatic fallback).
    IDWriteFontCollection *chooseFontCollection(const std::wstring &family) const
    {
        if (customFontCollection_ != nullptr) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(customFontCollection_->FindFamilyName(family.c_str(), &index, &exists))
                && exists) {
                return customFontCollection_;
            }
        }
        return systemFontCollection_;
    }

    ComPtr<IDWriteTextLayout> createLayout(const std::wstring &wide, const Paint &paint,
                                           float maxWidth) const
    {
        if (dwriteFactory_ == nullptr) {
            return nullptr;
        }

        const std::wstring family = toWideString(paint.hasFontFamily() ? paint.getFontFamily()
                                                                       : "Segoe UI");
        IDWriteFontCollection *fontCollection = chooseFontCollection(family);
        if (fontCollection == nullptr) {
            return nullptr;
        }

        const float fontSize = paint.getTextSize() > 0.0f ? paint.getTextSize() : 16.0f;

        // Locale drives locale-aware shaping and fallback (e.g. Han unification).
        const std::wstring locale = paint.hasTextLocale() ? toWideString(paint.getTextLocale())
                                                          : std::wstring(L"en-US");

        ComPtr<IDWriteTextFormat> format;
        HRESULT hr = dwriteFactory_->CreateTextFormat(
            family.c_str(), fontCollection, mapFontWeight(paint.getFontWeight()),
            mapFontSlant(paint.getFontSlant()), DWRITE_FONT_STRETCH_NORMAL, fontSize, locale.c_str(),
            &format);
        if (FAILED(hr) || format == nullptr) {
            return nullptr;
        }

        const float layoutMaxWidth = maxWidth > 0.0f ? maxWidth : 1e6f;
        const float layoutMaxHeight = 1e6f;

        ComPtr<IDWriteTextLayout> layout;
        hr = dwriteFactory_->CreateTextLayout(wide.c_str(), static_cast<UINT32>(wide.size()),
                                              format.Get(), layoutMaxWidth, layoutMaxHeight, &layout);
        if (FAILED(hr) || layout == nullptr) {
            return nullptr;
        }

        // Enable word wrapping if a max width was specified.
        if (maxWidth > 0.0f) {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        } else {
            layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        }

        // Apply a custom font fallback chain if one was configured.
        if (customFontFallback_ != nullptr) {
            ComPtr<IDWriteTextLayout2> layout2;
            if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout2),
                                                 reinterpret_cast<void **>(&layout2)))
                && layout2 != nullptr) {
                layout2->SetFontFallback(customFontFallback_);
            }
        }

        // Bake letter spacing into the layout (IDWriteTextLayout1) so measurement
        // and rendering stay consistent. Trailing spacing on every cluster except
        // the last yields (N-1) inter-glyph gaps.
        const float letterSpacing =
            std::isfinite(paint.getLetterSpacing()) ? paint.getLetterSpacing() : 0.0f;
        if (letterSpacing != 0.0f && wide.size() > 1) {
            ComPtr<IDWriteTextLayout1> layout1;
            if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1),
                                                 reinterpret_cast<void **>(&layout1)))
                && layout1 != nullptr) {
                const DWRITE_TEXT_RANGE range{0, static_cast<UINT32>(wide.size() - 1)};
                layout1->SetCharacterSpacing(0.0f, letterSpacing, 0.0f, range);
            }
        }

        return layout;
    }

    std::vector<unsigned char> renderLayoutToBitmap(IDWriteTextLayout *layout, int width, int height,
                                                     const Paint & /*paint*/) const
    {
        if (layout == nullptr || width <= 0 || height <= 0) {
            return {};
        }
        // Defense-in-depth clamp + overflow-safe allocation. Compute the byte
        // count in 64-bit so the multiply can't wrap, reject oversized requests,
        // and catch bad_alloc so a failure returns an empty result instead of
        // terminating the process.
        if (width > kMaxBitmapDimension || height > kMaxBitmapDimension) {
            return {};
        }
        const std::uint64_t byteCount =
            static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ull;
        if (byteCount > static_cast<std::uint64_t>(SIZE_MAX)) {
            return {};
        }
        std::vector<unsigned char> pixels;
        try {
            pixels.assign(static_cast<size_t>(byteCount), 0);
        } catch (const std::bad_alloc &) {
            return {};
        }

        // Ensure COM is initialized on this thread for WIC. Balance every
        // successful init (including S_FALSE) with CoUninitialize.
        const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool needComUninit = (comInit == S_OK || comInit == S_FALSE);
        struct ComGuard
        {
            bool active;
            ~ComGuard() { if (active) CoUninitialize(); }
        } comGuard{needComUninit};

        // WIC imaging factory + bitmap.
        ComPtr<IWICImagingFactory> wicFactory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      __uuidof(IWICImagingFactory),
                                      reinterpret_cast<void **>(&wicFactory));
        if (FAILED(hr) || !wicFactory) {
            return {};
        }

        ComPtr<IWICBitmap> wicBitmap;
        hr = wicFactory->CreateBitmap(static_cast<UINT>(width), static_cast<UINT>(height),
                                      GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &wicBitmap);
        if (FAILED(hr) || !wicBitmap) {
            return {};
        }

        // D2D factory + WIC-backed render target.
        ComPtr<ID2D1Factory> d2dFactory;
        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory);
        if (FAILED(hr) || !d2dFactory) {
            return {};
        }

        const bool clearType = options_.rasterMode == DirectWriteRasterMode::ClearType;
        D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
        // ClearType needs an opaque target; grayscale uses premultiplied alpha.
        rtProps.pixelFormat = D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM,
            clearType ? D2D1_ALPHA_MODE_IGNORE : D2D1_ALPHA_MODE_PREMULTIPLIED);

        ComPtr<ID2D1RenderTarget> renderTarget;
        hr = d2dFactory->CreateWicBitmapRenderTarget(wicBitmap.Get(), rtProps, &renderTarget);
        if (FAILED(hr) || !renderTarget) {
            return {};
        }

        renderTarget->BeginDraw();
        renderTarget->SetTransform(D2D1::IdentityMatrix());
        // Grayscale: clear transparent. ClearType: clear opaque black.
        renderTarget->Clear(clearType ? D2D1::ColorF(D2D1::ColorF::Black, 1.0f)
                                       : D2D1::ColorF(D2D1::ColorF::Black, 0.0f));

        ComPtr<ID2D1SolidColorBrush> brush;
        renderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 1.0f), &brush);
        if (!brush) {
            renderTarget->EndDraw();
            return {};
        }

        renderTarget->SetTextAntialiasMode(clearType ? D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE
                                                     : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        renderTarget->DrawTextLayout(D2D1::Point2F(0.0f, 0.0f), layout, brush.Get(),
                                     D2D1_DRAW_TEXT_OPTIONS_NONE);

        hr = renderTarget->EndDraw();
        if (FAILED(hr)) {
            return {};
        }

        // Read the pixels back from the WIC bitmap.
        ComPtr<IWICBitmapLock> lock;
        WICRect rect = {0, 0, width, height};
        hr = wicBitmap->Lock(&rect, WICBitmapLockRead, &lock);
        if (FAILED(hr) || !lock) {
            return {};
        }
        UINT stride = 0;
        UINT bufferSize = 0;
        BYTE *data = nullptr;
        lock->GetStride(&stride);
        hr = lock->GetDataPointer(&bufferSize, &data);
        if (FAILED(hr) || data == nullptr) {
            return {};
        }
        for (int row = 0; row < height; ++row) {
            const unsigned char *src = data + static_cast<size_t>(row) * stride;
            unsigned char *dst = pixels.data() + static_cast<size_t>(row) * width * 4;
            std::memcpy(dst, src, static_cast<size_t>(width) * 4);
        }

        // Convert BGRA -> RGBA coverage.
        for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
            const unsigned char b = pixels[i * 4 + 0];
            const unsigned char g = pixels[i * 4 + 1];
            const unsigned char r = pixels[i * 4 + 2];
            const unsigned char a = pixels[i * 4 + 3];
            if (clearType) {
                // White text over black => the RGB channels ARE the per-channel
                // ClearType coverage. Preserve subpixel RGB; derive alpha from the
                // brightest channel so it can still be composited.
                pixels[i * 4 + 0] = r;
                pixels[i * 4 + 1] = g;
                pixels[i * 4 + 2] = b;
                pixels[i * 4 + 3] = std::max(r, std::max(g, b));
            } else {
                // Grayscale white-over-transparent (premultiplied): the alpha IS
                // the coverage. Emit white RGB + coverage alpha.
                const unsigned char coverage = a != 0 ? a : std::max(r, std::max(g, b));
                pixels[i * 4 + 0] = 255;
                pixels[i * 4 + 1] = 255;
                pixels[i * 4 + 2] = 255;
                pixels[i * 4 + 3] = coverage;
            }
        }

        return pixels;
    }

    DirectWriteBackendOptions options_;
    IDWriteFactory *dwriteFactory_ = nullptr;
    IDWriteFontCollection *systemFontCollection_ = nullptr;
    IDWriteFontCollection *customFontCollection_ = nullptr;
    CustomFontCollectionLoader *collectionLoader_ = nullptr;
    IDWriteInMemoryFontFileLoader *memoryFontLoader_ = nullptr;
    IDWriteFontFallback *customFontFallback_ = nullptr;
    std::vector<CustomFontSource> customFontSources_;
    unsigned int collectionGeneration_ = 0;
    bool available_ = false;
    mutable std::vector<TextBackendDiagnostic> diagnostics_;
};

} // anonymous namespace

std::unique_ptr<ITextBackend> createDirectWriteTextBackend(const DirectWriteBackendOptions &options)
{
#ifdef _WIN32
    auto backend = std::make_unique<DirectWriteTextBackend>(options);
    if (!backend->isAvailable()) {
        return nullptr;
    }
    return backend;
#else
    (void)options;
    return nullptr;
#endif
}

#else // !_WIN32

std::unique_ptr<ITextBackend> createDirectWriteTextBackend(const DirectWriteBackendOptions & /*options*/)
{
    return nullptr;
}

#endif // _WIN32

} // namespace wsc::text
