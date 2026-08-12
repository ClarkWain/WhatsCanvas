#pragma once

#include <string>
#include <vector>

namespace wsc {
namespace detail {

struct DiscoveredFontFace
{
    std::string family;
    std::string path;
    int faceIndex = 0;
    int weight = 400;
    bool italic = false;
};

// Enumerates every font face the platform's native font manager knows about.
// macOS: CoreText (CTFontManagerCopyAvailableFontFamilyNames + CTFontDescriptor).
// Windows: DirectWrite (IDWriteFactory::GetSystemFontCollection).
// Linux: fontconfig (FcConfigGetFonts).
// Returns an empty vector when the platform API is unavailable.
std::vector<DiscoveredFontFace> discoverInstalledFontFaces();

} // namespace detail
} // namespace wsc
