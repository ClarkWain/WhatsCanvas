#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace whatscanvas::scenes {

struct SafeInsets
{
    float top = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
    float right = 0.0f;
};

struct CanonicalViewport
{
    float width = 0.0f;
    float height = 0.0f;
    float scale = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

// Validation coordinates are design-space units, not dimensions reported by
// any one device. The primary 2:1 canvas is round and rotation-symmetric.
// Other standards cover responsive layout classes; LegacyAndroid exists only
// for comparisons with captures made before the standard was introduced.
enum class ViewportStandard
{
    Phone2To1,
    CompactPhone16To9,
    Tablet4To3,
    Desktop16To10,
    LegacyAndroid
};

struct ViewportStandardSpec
{
    ViewportStandard standard;
    std::string_view id;
    float portraitWidth;
    float portraitHeight;
    float landscapeWidth;
    float landscapeHeight;
    bool primaryPixelGate;
};

inline constexpr std::array<ViewportStandardSpec, 5> kViewportStandards = {{
    {ViewportStandard::Phone2To1, "phone_2_1",
     400.0f, 800.0f, 800.0f, 400.0f, true},
    {ViewportStandard::CompactPhone16To9, "phone_16_9",
     360.0f, 640.0f, 640.0f, 360.0f, false},
    {ViewportStandard::Tablet4To3, "tablet_4_3",
     768.0f, 1024.0f, 1024.0f, 768.0f, false},
    {ViewportStandard::Desktop16To10, "desktop_16_10",
     900.0f, 1440.0f, 1440.0f, 900.0f, false},
    {ViewportStandard::LegacyAndroid, "legacy_android",
     393.0f, 759.0f, 786.0f, 377.0f, false},
}};

inline constexpr const ViewportStandardSpec& viewportStandardSpec(
    ViewportStandard standard)
{
    for (const auto& spec : kViewportStandards) {
        if (spec.standard == standard) return spec;
    }
    return kViewportStandards[0];
}

inline bool parseViewportStandard(std::string_view id, ViewportStandard& standard)
{
    for (const auto& spec : kViewportStandards) {
        if (spec.id == id) {
            standard = spec.standard;
            return true;
        }
    }
    return false;
}

// All platform hosts render validation scenes into the same logical content
// canvases. Device screens and desktop windows are containers only: after
// safe-area removal the canonical canvas is aspect-fitted as one unit.
inline CanonicalViewport makeCanonicalViewport(float hostWidth,
                                                float hostHeight,
                                                SafeInsets insets = {},
                                                ViewportStandard standard =
                                                    ViewportStandard::Phone2To1)
{
    const float availableWidth = std::max(
        0.0f, hostWidth - insets.left - insets.right);
    const float availableHeight = std::max(
        0.0f, hostHeight - insets.top - insets.bottom);
    const bool landscape = availableWidth > availableHeight;
    const auto& spec = viewportStandardSpec(standard);

    CanonicalViewport viewport;
    viewport.width = landscape ? spec.landscapeWidth : spec.portraitWidth;
    viewport.height = landscape ? spec.landscapeHeight : spec.portraitHeight;
    viewport.offsetX = insets.left;
    viewport.offsetY = insets.top;
    if (availableWidth <= 0.0f || availableHeight <= 0.0f) {
        return viewport;
    }

    viewport.scale = std::min(availableWidth / viewport.width,
                              availableHeight / viewport.height);
    viewport.offsetX +=
        (availableWidth - viewport.width * viewport.scale) * 0.5f;
    // Top anchoring is part of the scene contract. Any aspect-ratio remainder
    // stays outside the canonical scene instead of causing platform reflow.
    return viewport;
}

} // namespace whatscanvas::scenes
