#include "SoftwareRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <wsc/wsc.h>

namespace whatscanvas::desktop {

int SoftwareRuntime::runDump(IScene& scene, const SoftwareDumpConfig& config)
{
    if (config.outputPath.empty()) {
        std::fprintf(stderr, "[SoftwareRuntime] runDump requires outputPath\n");
        return 1;
    }

    const float devicePixelRatio = std::max(0.01f, config.devicePixelRatio);
    const float logicalWidth = static_cast<float>(std::max(1, config.width));
    const float logicalHeight = static_cast<float>(std::max(1, config.height));
    const int width = std::max(
        1, static_cast<int>(std::lround(logicalWidth * devicePixelRatio)));
    const int height = std::max(
        1, static_cast<int>(std::lround(logicalHeight * devicePixelRatio)));
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, width, height);
    if (!canvas) {
        std::fprintf(stderr, "[SoftwareRuntime] Canvas::create(Software) failed\n");
        return 4;
    }
    canvas->setDevicePixelRatio(devicePixelRatio);
    if (!canvas->initializeContext()) {
        std::fprintf(stderr, "[SoftwareRuntime] Canvas::initializeContext failed\n");
        return 4;
    }

#if defined(__APPLE__)
    if (!canvas->setTextBackend(wsc::Canvas::TextBackend::CoreText)) {
        std::fprintf(stderr, "[SoftwareRuntime] CoreText backend initialization failed\n");
        canvas->finalizeContext();
        return 5;
    }
#elif defined(_WIN32)
    if (!canvas->setTextBackend(wsc::Canvas::TextBackend::DirectWrite)) {
        std::fprintf(stderr, "[SoftwareRuntime] DirectWrite backend initialization failed\n");
        canvas->finalizeContext();
        return 5;
    }
#endif

    scene.onCanvasReady(*canvas);
    scene.onLayout(*canvas, logicalWidth, logicalHeight);

    const int frameCount = std::max(1, config.frames);
    for (int i = 0; i < frameCount; ++i) {
        canvas->beginFrame();
        const float elapsed = config.captureTimeSeconds >= 0.0f
            ? config.captureTimeSeconds
            : static_cast<float>(i) * config.frameDeltaSeconds;
        scene.onFrame(*canvas, FrameInfo{logicalWidth, logicalHeight, elapsed, i});
        canvas->endFrame();
    }

    const bool saved = canvas->savePixelsPPM(config.outputPath);
    if (!saved) {
        std::fprintf(stderr, "[SoftwareRuntime] savePixelsPPM(%s) failed\n",
                     config.outputPath.c_str());
    } else {
        std::fprintf(stdout,
                     "[SoftwareRuntime] wrote %s (%dx%d, %.2fx, %d frame(s))\n",
                     config.outputPath.c_str(), width, height,
                     devicePixelRatio, frameCount);
    }

    scene.onCanvasReleasing();
    canvas->finalizeContext();
    return saved ? 0 : 6;
}

} // namespace whatscanvas::desktop
