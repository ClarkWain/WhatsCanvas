#pragma once

#include <algorithm>

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

// All platform hosts render validation scenes into the same logical content
// canvases. Device screens and desktop windows are containers only: after
// safe-area removal the canonical canvas is aspect-fitted as one unit.
inline CanonicalViewport makeCanonicalViewport(float hostWidth,
                                                float hostHeight,
                                                SafeInsets insets = {})
{
    constexpr float landscapeWidth = 786.0f;
    constexpr float landscapeHeight = 377.0f;
    constexpr float portraitWidth = 393.0f;
    constexpr float portraitHeight = 759.0f;

    const float availableWidth = std::max(
        0.0f, hostWidth - insets.left - insets.right);
    const float availableHeight = std::max(
        0.0f, hostHeight - insets.top - insets.bottom);
    const bool landscape = availableWidth > availableHeight;

    CanonicalViewport viewport;
    viewport.width = landscape ? landscapeWidth : portraitWidth;
    viewport.height = landscape ? landscapeHeight : portraitHeight;
    viewport.offsetX = insets.left;
    viewport.offsetY = insets.top;
    if (availableWidth <= 0.0f || availableHeight <= 0.0f) {
        return viewport;
    }

    viewport.scale = std::min(availableWidth / viewport.width,
                              availableHeight / viewport.height);
    viewport.offsetX +=
        (availableWidth - viewport.width * viewport.scale) * 0.5f;
    // Match Android's full-screen surface: anchor content to the usable top
    // and leave any aspect-ratio remainder below the scene.
    return viewport;
}

} // namespace whatscanvas::scenes
