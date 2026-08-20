#pragma once

#include "../../shared/scenes/CanonicalViewport.h"

namespace whatscanvas::desktop {

using SceneViewport = scenes::CanonicalViewport;

inline SceneViewport makeFeatureShowcaseViewport(float hostWidth,
                                                 float hostHeight)
{
    return scenes::makeCanonicalViewport(hostWidth, hostHeight);
}

} // namespace whatscanvas::desktop
