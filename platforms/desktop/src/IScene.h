#pragma once

// Portable scene contract for the WhatsCanvas platform hosts.
//
// A Scene is a self-contained piece of drawing content that renders through
// the public wsc::Canvas API only. It knows nothing about the host window,
// swapchain, event loop, backend, or platform. The same Scene can be driven
// by any host (Desktop/GLFW today; Android/iOS/Web later) so that all four
// platforms display exactly the same visual content and share regression
// baselines.
//
// Lifecycle from the host's perspective:
//   1. Host creates a wsc::Canvas and calls Canvas::initializeContext().
//   2. Host calls Scene::onCanvasReady(canvas)             (upload GPU resources)
//   3. Host calls Scene::onLayout(canvas, w, h)            (record retained Pictures)
//      -- called again on every logical-size change --
//   4. Per frame: Canvas::beginFrame(); Scene::onFrame(...); Canvas::endFrame().
//   5. On teardown Host calls Scene::onCanvasReleasing()   (drop GPU resources)
//      before Canvas::finalizeContext().

#include <memory>
#include <string>

namespace wsc { class Canvas; }

namespace whatscanvas::desktop {

struct FrameInfo
{
    float logicalWidth = 0.0f;
    float logicalHeight = 0.0f;
    float elapsedSeconds = 0.0f;
    int frameIndex = 0;
};

class IScene
{
public:
    virtual ~IScene() = default;

    virtual const char* name() const = 0;

    virtual void onCanvasReady(wsc::Canvas& canvas) = 0;
    virtual void onLayout(wsc::Canvas& canvas, float logicalWidth, float logicalHeight) = 0;
    virtual void onFrame(wsc::Canvas& canvas, const FrameInfo& info) = 0;
    virtual void onCanvasReleasing() = 0;
};

using ScenePtr = std::unique_ptr<IScene>;

} // namespace whatscanvas::desktop
