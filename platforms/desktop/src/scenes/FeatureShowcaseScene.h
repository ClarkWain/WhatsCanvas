#pragma once

#include <memory>
#include <string>

#include "../IScene.h"
#include "../../../shared/scenes/CanonicalViewport.h"

namespace wsc { class Image; class Picture; }

namespace whatscanvas::desktop {

struct FeatureShowcaseBranding
{
    std::string title = "WhatsCanvas Desktop";
    std::string subtitle = "OpenGL 3.3  |  live feature matrix";
    std::string footer = "8 feature cards  |  real OpenGL output";
};

// Eight-card feature matrix that mirrors the Android host card-for-card. The
// static card chrome, labels and paths are recorded once into a retained
// Picture and drawn each frame through drawPictureRasterized(); only the
// genuinely animated overlays are re-recorded per frame.
class FeatureShowcaseScene final : public IScene
{
public:
    explicit FeatureShowcaseScene(
        FeatureShowcaseBranding branding = {},
        whatscanvas::scenes::ViewportStandard viewportStandard =
            whatscanvas::scenes::ViewportStandard::Phone2To1);
    ~FeatureShowcaseScene() override;

    const char* name() const override { return "feature_showcase"; }

    void onCanvasReady(wsc::Canvas& canvas) override;
    void onLayout(wsc::Canvas& canvas, float logicalWidth, float logicalHeight) override;
    void onFrame(wsc::Canvas& canvas, const FrameInfo& info) override;
    void onCanvasReleasing() override;

private:
    FeatureShowcaseBranding branding_;
    whatscanvas::scenes::ViewportStandard viewportStandard_;
    std::unique_ptr<wsc::Image> checkerImage_;
    std::shared_ptr<const wsc::Picture> staticPicture_;
    float sceneWidth_ = 0.0f;
    float sceneHeight_ = 0.0f;
    float sceneScale_ = 1.0f;
    float sceneOffsetX_ = 0.0f;
    float sceneOffsetY_ = 0.0f;
};

} // namespace whatscanvas::desktop
