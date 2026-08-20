#pragma once

#include <algorithm>

namespace whatscanvas::desktop {

// Reference logical viewport measured from the Android feature showcase. The
// desktop host aspect-fits this canvas and scales it as one unit so typography,
// radii, gaps, and feature geometry keep the same proportions as Android.
struct SceneViewport
{
    float width = 0.0f;
    float height = 0.0f;
    float scale = 1.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

inline SceneViewport makeFeatureShowcaseViewport(float hostWidth,
                                                 float hostHeight)
{
    constexpr float landscapeWidth = 786.0f;
    constexpr float landscapeHeight = 377.0f;
    constexpr float portraitWidth = 393.0f;
    constexpr float portraitHeight = 759.0f;
    const bool landscape = hostWidth > hostHeight;

    SceneViewport viewport;
    viewport.width = landscape ? landscapeWidth : portraitWidth;
    viewport.height = landscape ? landscapeHeight : portraitHeight;
    if (hostWidth <= 0.0f || hostHeight <= 0.0f) {
        return viewport;
    }

    viewport.scale = std::min(hostWidth / viewport.width,
                              hostHeight / viewport.height);
    viewport.offsetX = (hostWidth - viewport.width * viewport.scale) * 0.5f;
    // Android lays the render surface out from the top and leaves system UI at
    // the bottom. Keep the same anchor when the desktop window is slightly
    // taller than the reference aspect ratio.
    viewport.offsetY = 0.0f;
    return viewport;
}

} // namespace whatscanvas::desktop
