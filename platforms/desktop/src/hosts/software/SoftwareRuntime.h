#pragma once

#include <string>

#include "../../IScene.h"

namespace whatscanvas::desktop {

struct SoftwareDumpConfig
{
    std::string outputPath;   // .ppm file; empty means dump mode is not run.
    int width = 1280;          // Logical pixels; output width is width * DPR.
    int height = 720;          // Logical pixels; output height is height * DPR.
    int frames = 1;
    float frameDeltaSeconds = 1.0f / 60.0f;
    float captureTimeSeconds = -1.0f;
    float devicePixelRatio = 1.0f;
};

// Headless CPU renderer. Does not require GLFW, an OpenGL loader, an
// interactive user session, or a display server. Ideal for CI regressions on
// hosted Windows / macOS runners that cannot create even a hidden GLFW window.
class SoftwareRuntime
{
public:
    static int runDump(IScene& scene, const SoftwareDumpConfig& config);
};

} // namespace whatscanvas::desktop
