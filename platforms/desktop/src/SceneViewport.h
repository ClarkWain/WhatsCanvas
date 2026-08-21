#pragma once

#include "../../shared/scenes/CanonicalViewport.h"

namespace whatscanvas::desktop {

using SceneViewport = scenes::CanonicalViewport;

inline SceneViewport makeFeatureShowcaseViewport(float hostWidth,
                                                 float hostHeight,
                                                 scenes::ViewportStandard standard =
                                                     scenes::ViewportStandard::Phone2To1)
{
    return scenes::makeCanonicalViewport(hostWidth, hostHeight, {}, standard);
}

} // namespace whatscanvas::desktop
