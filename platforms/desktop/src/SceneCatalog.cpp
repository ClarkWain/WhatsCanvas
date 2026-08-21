#include "SceneCatalog.h"

#include "scenes/FeatureShowcaseScene.h"
#include "scenes/StressScene.h"

namespace whatscanvas::desktop {

namespace {

constexpr const char* kSceneNames[] = {
    "feature_showcase",
    "text_stress",
    "geometry_stress",
    "compositing_stress"
};

} // namespace

ScenePtr SceneCatalog::create(const std::string& name,
                              scenes::ViewportStandard viewportStandard)
{
    if (name == kSceneNames[0]) {
        return std::make_unique<FeatureShowcaseScene>(
            FeatureShowcaseBranding{}, viewportStandard);
    }
    scenes::StressSceneId stressId;
    if (scenes::parseStressScene(name, stressId)) {
        return std::make_unique<StressScene>(stressId, viewportStandard);
    }
    return nullptr;
}

std::vector<std::string> SceneCatalog::listNames()
{
    std::vector<std::string> names;
    names.reserve(sizeof(kSceneNames) / sizeof(kSceneNames[0]));
    for (const char* name : kSceneNames) {
        names.emplace_back(name);
    }
    return names;
}

const char* SceneCatalog::defaultName()
{
    return kSceneNames[0];
}

} // namespace whatscanvas::desktop
