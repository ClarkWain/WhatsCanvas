#pragma once

#include <string>

#include "../../IScene.h"

namespace whatscanvas::desktop {

struct GlfwHostConfig
{
    std::string title = "WhatsCanvas Desktop";
    int width = 1280;
    int height = 720;
    bool disableMsaa = false;
    bool vsync = true;
};

struct GlfwDumpConfig
{
    std::string outputPath;   // .ppm file. If empty, dump mode is not run.
    int width = 1280;
    int height = 720;
    int frames = 1;
    float frameDeltaSeconds = 1.0f / 60.0f;
};

struct GlfwBenchmarkConfig
{
    int width = 1280;
    int height = 720;
    int warmupFrames = 30;
    int measuredFrames = 300;
    float frameDeltaSeconds = 1.0f / 60.0f;
};

class GlfwHost
{
public:
    // Windowed run loop. Returns 0 on clean shutdown, non-zero on init failure.
    static int runInteractive(IScene& scene, const GlfwHostConfig& config);

    // Off-screen: advance `frames` frames, capture the final one, write PPM.
    static int runDump(IScene& scene, const GlfwDumpConfig& config);

    // Off-screen: advance warmup+measured frames, print timings + render stats.
    static int runBenchmark(IScene& scene, const GlfwBenchmarkConfig& config);
};

} // namespace whatscanvas::desktop
