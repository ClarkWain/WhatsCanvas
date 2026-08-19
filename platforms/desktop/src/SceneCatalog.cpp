#include "SceneCatalog.h"

#include "scenes/FeatureShowcaseScene.h"

namespace whatscanvas::desktop {

namespace {

struct Entry
{
    const char* name;
    ScenePtr (*factory)();
};

const Entry kEntries[] = {
    { "feature_showcase", []() -> ScenePtr {
          return std::make_unique<FeatureShowcaseScene>();
      } },
};

} // namespace

ScenePtr SceneCatalog::create(const std::string& name)
{
    for (const auto& entry : kEntries) {
        if (name == entry.name) {
            return entry.factory();
        }
    }
    return nullptr;
}

std::vector<std::string> SceneCatalog::listNames()
{
    std::vector<std::string> names;
    names.reserve(sizeof(kEntries) / sizeof(kEntries[0]));
    for (const auto& entry : kEntries) {
        names.emplace_back(entry.name);
    }
    return names;
}

const char* SceneCatalog::defaultName()
{
    return kEntries[0].name;
}

} // namespace whatscanvas::desktop
