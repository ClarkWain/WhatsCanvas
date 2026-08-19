#include "SoftwareRuntime.h"

#include <algorithm>
#include <cstdio>

#include <wsc/wsc.h>

namespace whatscanvas::desktop {

int SoftwareRuntime::runDump(IScene& scene, const SoftwareDumpConfig& config)
{
    if (config.outputPath.empty()) {
        std::fprintf(stderr, "[SoftwareRuntime] runDump requires outputPath\n");
        return 1;
    }

    const int width = std::max(1, config.width);
    const int height = std::max(1, config.height);
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, width, height);
    if (!canvas || !canvas->initializeContext()) {
        std::fprintf(stderr, "[SoftwareRuntime] Canvas::create(Software) failed\n");
        return 4;
    }

    scene.onCanvasReady(*canvas);
    const float logicalWidth = static_cast<float>(width);
    const float logicalHeight = static_cast<float>(height);
    scene.onLayout(*canvas, logicalWidth, logicalHeight);

    const int frameCount = std::max(1, config.frames);
    for (int i = 0; i < frameCount; ++i) {
        canvas->beginFrame();
        const float elapsed = static_cast<float>(i) * config.frameDeltaSeconds;
        scene.onFrame(*canvas, FrameInfo{logicalWidth, logicalHeight, elapsed, i});
        canvas->endFrame();
    }

    const bool saved = canvas->savePixelsPPM(config.outputPath);
    if (!saved) {
        std::fprintf(stderr, "[SoftwareRuntime] savePixelsPPM(%s) failed\n",
                     config.outputPath.c_str());
    } else {
        std::fprintf(stdout, "[SoftwareRuntime] wrote %s (%dx%d, %d frame(s))\n",
                     config.outputPath.c_str(), width, height, frameCount);
    }

    scene.onCanvasReleasing();
    canvas->finalizeContext();
    return saved ? 0 : 6;
}

} // namespace whatscanvas::desktop
