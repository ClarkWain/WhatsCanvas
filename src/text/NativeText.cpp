#include "text/NativeText.h"

#include <algorithm>
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef WINDING
#undef WINDING
#endif
#endif

namespace {

constexpr float kPointEpsilon = 0.0001f;

#ifdef _WIN32
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

HFONT createNativeFont(const Paint &paint)
{
    const std::wstring family = toWideString(paint.getFontFamily());
    if (family.empty() || paint.getTextSize() <= 0.0f) {
        return nullptr;
    }

    const int pixelHeight = -std::max(1, static_cast<int>(std::round(paint.getTextSize())));
    const BOOL italic = paint.getFontSlant() == wsc::FontSlant::NORMAL ? FALSE : TRUE;
    return CreateFontW(pixelHeight, 0, 0, 0, paint.getFontWeight(), italic, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family.c_str());
}
#endif

} // namespace

namespace wsc::text {

NativeTextMeasure measureNativeText(const std::string &text, const Paint &paint)
{
    NativeTextMeasure result;
#ifdef _WIN32
    if (!paint.hasFontFamily() || text.empty() || paint.getTextSize() <= 0.0f) {
        return result;
    }

    const std::wstring wideText = toWideString(text);
    if (wideText.empty()) {
        return result;
    }

    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = createNativeFont(paint);
    if (dc == nullptr || font == nullptr) {
        if (font != nullptr) {
            DeleteObject(font);
        }
        if (dc != nullptr) {
            DeleteDC(dc);
        }
        return result;
    }

    HGDIOBJ previousFont = SelectObject(dc, font);
    TEXTMETRICW textMetric = {};
    SIZE size = {0, 0};
    const bool hasMetrics = GetTextMetricsW(dc, &textMetric) != FALSE;
    const bool hasExtent = GetTextExtentPoint32W(dc, wideText.c_str(), static_cast<int>(wideText.size()), &size) != FALSE;
    SelectObject(dc, previousFont);
    DeleteObject(font);
    DeleteDC(dc);
    if (!hasMetrics || !hasExtent) {
        return result;
    }

    const float letterSpacing = std::isfinite(paint.getLetterSpacing()) ? paint.getLetterSpacing() : 0.0f;
    const float spacingWidth = wideText.size() > 1 ? letterSpacing * static_cast<float>(wideText.size() - 1) : 0.0f;
    const float width = std::max(1.0f, static_cast<float>(size.cx) + spacingWidth);
    const float height = std::max(1.0f, static_cast<float>(textMetric.tmHeight));
    result.valid = true;
    result.pixelWidth = std::max(1, static_cast<int>(std::ceil(width)));
    result.pixelHeight = std::max(1, static_cast<int>(std::ceil(height)));
    result.width = static_cast<float>(result.pixelWidth);
    result.height = static_cast<float>(result.pixelHeight);
    result.alphabeticBaseline = std::clamp(
        static_cast<float>(textMetric.tmAscent), 0.0f, result.height);
#else
    (void)text;
    (void)paint;
#endif
    return result;
}

NativeTextBitmap renderNativeTextBitmap(const std::string &text, const Paint &paint, const NativeTextMeasure &measure)
{
    NativeTextBitmap bitmap;
#ifdef _WIN32
    if (!measure.valid || measure.pixelWidth <= 0 || measure.pixelHeight <= 0) {
        return bitmap;
    }

    const std::wstring wideText = toWideString(text);
    if (wideText.empty()) {
        return bitmap;
    }

    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = createNativeFont(paint);
    if (dc == nullptr || font == nullptr) {
        if (font != nullptr) {
            DeleteObject(font);
        }
        if (dc != nullptr) {
            DeleteDC(dc);
        }
        return bitmap;
    }

    // TextOutW may touch the pixel immediately past its logical advance due
    // to ClearType/antialias overhang. A one-pixel transparent apron prevents
    // the final glyph (notably the S in compact all-caps labels) from being
    // cut off by the temporary DIB while preserving the measured advance.
    constexpr int kHorizontalPadding = 1;
    const int bitmapWidth = measure.pixelWidth + kHorizontalPadding * 2;

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = bitmapWidth;
    bitmapInfo.bmiHeader.biHeight = -measure.pixelHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP dib = CreateDIBSection(dc, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib == nullptr || bits == nullptr) {
        if (dib != nullptr) {
            DeleteObject(dib);
        }
        DeleteObject(font);
        DeleteDC(dc);
        return bitmap;
    }

    std::memset(bits, 0, static_cast<size_t>(bitmapWidth) * static_cast<size_t>(measure.pixelHeight) * 4);
    HGDIOBJ previousBitmap = SelectObject(dc, dib);
    HGDIOBJ previousFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    SetTextAlign(dc, TA_LEFT | TA_TOP | TA_NOUPDATECP);

    const float letterSpacing = std::isfinite(paint.getLetterSpacing()) ? paint.getLetterSpacing() : 0.0f;
    if (std::abs(letterSpacing) <= kPointEpsilon) {
        TextOutW(dc, kHorizontalPadding, 0, wideText.c_str(), static_cast<int>(wideText.size()));
    } else {
        float cursorX = static_cast<float>(kHorizontalPadding);
        for (wchar_t character : wideText) {
            TextOutW(dc, static_cast<int>(std::round(cursorX)), 0, &character, 1);
            SIZE glyphSize = {0, 0};
            GetTextExtentPoint32W(dc, &character, 1, &glyphSize);
            cursorX += static_cast<float>(glyphSize.cx) + letterSpacing;
        }
    }

    bitmap.width = bitmapWidth;
    bitmap.height = measure.pixelHeight;
    bitmap.leftPadding = kHorizontalPadding;
    bitmap.rightPadding = kHorizontalPadding;
    bitmap.pixels.resize(static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height) * 4);
    const unsigned char *src = static_cast<const unsigned char *>(bits);
    bool hasIndependentRgbCoverage = false;
    for (size_t i = 0; i < static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height); ++i) {
        const unsigned char blue = src[i * 4 + 0];
        const unsigned char green = src[i * 4 + 1];
        const unsigned char red = src[i * 4 + 2];
        const unsigned char alpha = std::max(red, std::max(green, blue));
        // TextOutW on this 32-bit DIB leaves the three ClearType coverages in
        // B/G/R.  The old code deliberately collapsed them to one alpha mask,
        // which made the native path no sharper than a grayscale atlas after
        // OpenGL composition.  Preserve RGB coverage exactly, in RGBA order.
        bitmap.pixels[i * 4 + 0] = red;
        bitmap.pixels[i * 4 + 1] = green;
        bitmap.pixels[i * 4 + 2] = blue;
        bitmap.pixels[i * 4 + 3] = alpha;
        hasIndependentRgbCoverage = hasIndependentRgbCoverage
            || red != green || green != blue;
    }
    bitmap.isClearType = hasIndependentRgbCoverage;

    SelectObject(dc, previousFont);
    SelectObject(dc, previousBitmap);
    DeleteObject(dib);
    DeleteObject(font);
    DeleteDC(dc);
#else
    (void)text;
    (void)paint;
    (void)measure;
#endif
    return bitmap;
}

} // namespace wsc::text
