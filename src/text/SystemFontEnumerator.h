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

#if defined(_WIN32)
// Resolve the small set of default fallback families without opening every
// installed font face. A cold default lookup may use DirectWrite's cached
// collection; an explicit alias refresh asks for a fresh one when it queries
// DirectWrite. Full enumeration remains available above.
std::vector<DiscoveredFontFace> discoverSelectedFontFaces(
    const std::vector<std::string> &families, bool refreshCollection);
#endif

} // namespace detail
} // namespace wsc
