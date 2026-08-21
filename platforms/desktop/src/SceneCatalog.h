#pragma once

#include <string>
#include <vector>

#include "IScene.h"
#include "../../shared/scenes/CanonicalViewport.h"

namespace whatscanvas::desktop {

// Central registry of built-in scenes. Add new scenes by editing
// SceneCatalog.cpp; hosts remain agnostic and only ever look them up by name.
class SceneCatalog
{
public:
    static ScenePtr create(
        const std::string& name,
        scenes::ViewportStandard viewportStandard =
            scenes::ViewportStandard::Phone2To1);
    static std::vector<std::string> listNames();
    static const char* defaultName();
};

} // namespace whatscanvas::desktop
