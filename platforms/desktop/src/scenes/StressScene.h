#pragma once

#include "../IScene.h"
#include "../../../shared/scenes/CanonicalViewport.h"
#include "../../../shared/scenes/StressScenes.h"

namespace whatscanvas::desktop {

class StressScene final : public IScene
{
public:
    StressScene(scenes::StressSceneId id,
                scenes::ViewportStandard standard =
                    scenes::ViewportStandard::Phone2To1)
        : id_(id), standard_(standard)
    {
    }

    const char* name() const override { return scenes::stressSceneName(id_); }
    void onCanvasReady(wsc::Canvas&) override {}

    void onLayout(wsc::Canvas&, float width, float height) override
    {
        viewport_ = scenes::makeCanonicalViewport(width, height, {}, standard_);
    }

    void onFrame(wsc::Canvas& canvas, const FrameInfo& info) override
    {
        canvas.drawColor(wsc::Color(7, 11, 27));
        canvas.save();
        canvas.translate(viewport_.offsetX, viewport_.offsetY);
        canvas.scale(viewport_.scale, viewport_.scale);
        scenes::drawStressScene(canvas, id_, viewport_.width, viewport_.height,
                                info.elapsedSeconds);
        canvas.restore();
    }

    void onCanvasReleasing() override {}

private:
    scenes::StressSceneId id_;
    scenes::ViewportStandard standard_;
    scenes::CanonicalViewport viewport_;
};

} // namespace whatscanvas::desktop
