#include "GlfwHost.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include <GLFW/glfw3.h>
#include <wsc/wsc.h>

namespace whatscanvas::desktop {

namespace {

bool initializeGlfwOnce()
{
    static bool initialized = []() {
        if (!glfwInit()) {
            std::fprintf(stderr, "[GlfwHost] glfwInit failed\n");
            return false;
        }
        std::atexit([]() { glfwTerminate(); });
        return true;
    }();
    return initialized;
}

void applyGlHints(bool disableMsaa, bool retinaFramebuffer)
{
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, disableMsaa ? 0 : 4);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER,
                   retinaFramebuffer ? GLFW_TRUE : GLFW_FALSE);
#else
    (void)retinaFramebuffer;
#endif
}

void prepareDefaultFramebuffer(int width, int height)
{
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.03f, 0.04f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

bool selectNativeTextBackend(wsc::Canvas& canvas)
{
#if defined(__APPLE__)
    return canvas.setTextBackend(wsc::Canvas::TextBackend::CoreText);
#elif defined(_WIN32)
    return canvas.setTextBackend(wsc::Canvas::TextBackend::DirectWrite);
#else
    (void)canvas;
    return true;
#endif
}

// Convert a wsc-native OpenGLProcAddress request into a GLFW-supplied loader.
void* glfwLoader(const char* name)
{
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

struct FramebufferSize
{
    int width = 0;
    int height = 0;
    float contentScaleX = 1.0f;
    float contentScaleY = 1.0f;
};

FramebufferSize queryFramebufferSize(GLFWwindow* window)
{
    FramebufferSize size;
    glfwGetFramebufferSize(window, &size.width, &size.height);
    glfwGetWindowContentScale(window, &size.contentScaleX, &size.contentScaleY);
    if (!std::isfinite(size.contentScaleX) || size.contentScaleX <= 0.0f) {
        size.contentScaleX = 1.0f;
    }
    if (!std::isfinite(size.contentScaleY) || size.contentScaleY <= 0.0f) {
        size.contentScaleY = 1.0f;
    }
    return size;
}

} // namespace

int GlfwHost::runInteractive(IScene& scene, const GlfwHostConfig& config)
{
    if (!initializeGlfwOnce()) {
        return 1;
    }
    applyGlHints(config.disableMsaa, true);

    GLFWwindow* window = glfwCreateWindow(
        config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[GlfwHost] glfwCreateWindow failed\n");
        return 2;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(config.vsync ? 1 : 0);

    if (!wsc::Canvas::loadOpenGL(&glfwLoader)) {
        std::fprintf(stderr, "[GlfwHost] Canvas::loadOpenGL failed\n");
        glfwDestroyWindow(window);
        return 3;
    }

    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, config.width, config.height);
    if (!canvas) {
        std::fprintf(stderr, "[GlfwHost] Canvas::create(OpenGL) failed\n");
        glfwDestroyWindow(window);
        return 4;
    }

    if (!canvas->initializeContext()) {
        std::fprintf(stderr, "[GlfwHost] Canvas::initializeContext failed\n");
        glfwDestroyWindow(window);
        return 5;
    }

    if (!selectNativeTextBackend(*canvas)) {
        std::fprintf(stderr, "[GlfwHost] native text backend initialization failed\n");
        canvas->finalizeContext();
        glfwDestroyWindow(window);
        return 6;
    }

    scene.onCanvasReady(*canvas);

    FramebufferSize lastFramebuffer{};
    float lastDpr = 0.0f;
    const auto startTime = std::chrono::steady_clock::now();
    int frameIndex = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        const FramebufferSize fb = queryFramebufferSize(window);
        if (fb.width <= 0 || fb.height <= 0) {
            continue;
        }

        const float dpr = fb.contentScaleX;
        if (fb.width != lastFramebuffer.width || fb.height != lastFramebuffer.height ||
            std::abs(dpr - lastDpr) > 0.001f) {
            glViewport(0, 0, fb.width, fb.height);
            canvas->setSize(fb.width, fb.height);
            canvas->setDevicePixelRatio(dpr);
            const float logicalWidth = static_cast<float>(fb.width) / dpr;
            const float logicalHeight = static_cast<float>(fb.height) / dpr;
            scene.onLayout(*canvas, logicalWidth, logicalHeight);
            lastFramebuffer = fb;
            lastDpr = dpr;
        }

        const float elapsedSeconds = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - startTime).count();
        const float logicalWidth = static_cast<float>(fb.width) / dpr;
        const float logicalHeight = static_cast<float>(fb.height) / dpr;

        prepareDefaultFramebuffer(fb.width, fb.height);
        canvas->beginFrame();
        scene.onFrame(*canvas, FrameInfo{logicalWidth, logicalHeight,
                                          elapsedSeconds, frameIndex});
        canvas->endFrame();

        glfwSwapBuffers(window);
        ++frameIndex;
    }

    scene.onCanvasReleasing();
    canvas->finalizeContext();
    canvas.reset();
    glfwDestroyWindow(window);
    return 0;
}

int GlfwHost::runDump(IScene& scene, const GlfwDumpConfig& config)
{
    if (config.outputPath.empty()) {
        std::fprintf(stderr, "[GlfwHost] runDump requires outputPath\n");
        return 1;
    }
    if (!initializeGlfwOnce()) {
        return 1;
    }
    // Dump dimensions are physical pixels. Disabling the Retina backing store
    // keeps a requested 1280x720 capture at exactly 1280x720 on macOS.
    applyGlHints(true, false);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(
        std::max(1, config.width), std::max(1, config.height),
        "WhatsCanvas Desktop (dump)", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[GlfwHost] hidden glfwCreateWindow failed\n");
        return 2;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    if (!wsc::Canvas::loadOpenGL(&glfwLoader)) {
        std::fprintf(stderr, "[GlfwHost] Canvas::loadOpenGL failed\n");
        glfwDestroyWindow(window);
        return 3;
    }

    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, config.width, config.height);
    if (!canvas) {
        std::fprintf(stderr, "[GlfwHost] Canvas::create(OpenGL) failed\n");
        glfwDestroyWindow(window);
        return 4;
    }

    if (!canvas->initializeContext()) {
        std::fprintf(stderr, "[GlfwHost] Canvas::initializeContext failed\n");
        glfwDestroyWindow(window);
        return 5;
    }

    if (!selectNativeTextBackend(*canvas)) {
        std::fprintf(stderr, "[GlfwHost] native text backend initialization failed\n");
        canvas->finalizeContext();
        glfwDestroyWindow(window);
        return 6;
    }

    scene.onCanvasReady(*canvas);
    const float logicalWidth = static_cast<float>(config.width);
    const float logicalHeight = static_cast<float>(config.height);
    scene.onLayout(*canvas, logicalWidth, logicalHeight);

    const int frameCount = std::max(1, config.frames);
    for (int i = 0; i < frameCount; ++i) {
        prepareDefaultFramebuffer(config.width, config.height);
        canvas->beginFrame();
        const float elapsed = static_cast<float>(i) * config.frameDeltaSeconds;
        scene.onFrame(*canvas, FrameInfo{logicalWidth, logicalHeight, elapsed, i});
        canvas->endFrame();
    }

    const bool saved = canvas->savePixelsPPM(config.outputPath);
    if (!saved) {
        std::fprintf(stderr, "[GlfwHost] savePixelsPPM(%s) failed\n",
                     config.outputPath.c_str());
    } else {
        std::fprintf(stdout, "[GlfwHost] wrote %s (%dx%d, %d frame(s))\n",
                     config.outputPath.c_str(), config.width, config.height, frameCount);
    }

    scene.onCanvasReleasing();
    canvas->finalizeContext();
    canvas.reset();
    glfwDestroyWindow(window);
    return saved ? 0 : 6;
}

int GlfwHost::runBenchmark(IScene& scene, const GlfwBenchmarkConfig& config)
{
    if (!initializeGlfwOnce()) {
        return 1;
    }
    // Benchmark dimensions are physical pixels for cross-machine comparison.
    applyGlHints(true, false);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(
        std::max(1, config.width), std::max(1, config.height),
        "WhatsCanvas Desktop (benchmark)", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[GlfwHost] hidden glfwCreateWindow failed\n");
        return 2;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);

    if (!wsc::Canvas::loadOpenGL(&glfwLoader)) {
        std::fprintf(stderr, "[GlfwHost] Canvas::loadOpenGL failed\n");
        glfwDestroyWindow(window);
        return 3;
    }

    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, config.width, config.height);
    if (!canvas || !canvas->initializeContext()) {
        std::fprintf(stderr, "[GlfwHost] canvas init failed\n");
        glfwDestroyWindow(window);
        return 4;
    }
    if (!selectNativeTextBackend(*canvas)) {
        std::fprintf(stderr, "[GlfwHost] native text backend initialization failed\n");
        canvas->finalizeContext();
        glfwDestroyWindow(window);
        return 5;
    }
    canvas->setGpuTimingEnabled(true);

    scene.onCanvasReady(*canvas);
    const float logicalWidth = static_cast<float>(config.width);
    const float logicalHeight = static_cast<float>(config.height);
    scene.onLayout(*canvas, logicalWidth, logicalHeight);

    const int warmup = std::max(0, config.warmupFrames);
    const int measured = std::max(1, config.measuredFrames);

    // Warm-up: shader compile, glyph atlas, picture rasterization all pay their
    // one-time costs here, so measured numbers reflect steady state.
    for (int i = 0; i < warmup; ++i) {
        prepareDefaultFramebuffer(config.width, config.height);
        canvas->beginFrame();
        scene.onFrame(*canvas,
                      FrameInfo{logicalWidth, logicalHeight,
                                static_cast<float>(i) * config.frameDeltaSeconds, i});
        canvas->endFrame();
        glFinish();
    }

    std::vector<double> frameMs;
    frameMs.reserve(static_cast<size_t>(measured));
    std::uint64_t sumFlushCpuNs = 0;
    std::uint64_t sumGpuNs = 0;
    std::size_t gpuSamples = 0;
    std::size_t sumDrawCalls = 0;
    std::size_t sumCommandCount = 0;
    std::size_t sumMergedBatches = 0;
    std::size_t sumRasterHits = 0;
    std::size_t sumRasterMisses = 0;

    const auto benchmarkStart = std::chrono::steady_clock::now();
    for (int i = 0; i < measured; ++i) {
        const int frameIndex = warmup + i;
        const auto frameStart = std::chrono::steady_clock::now();
        prepareDefaultFramebuffer(config.width, config.height);
        canvas->beginFrame();
        scene.onFrame(*canvas,
                      FrameInfo{logicalWidth, logicalHeight,
                                static_cast<float>(frameIndex) * config.frameDeltaSeconds,
                                frameIndex});
        canvas->endFrame();
        glFinish();
        const auto frameEnd = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(
            frameEnd - frameStart).count();
        frameMs.push_back(ms);

        const auto stats = canvas->getRenderStats();
        sumFlushCpuNs += stats.flushCpuTimeNs;
        if (stats.gpuTimeAvailable) {
            sumGpuNs += stats.gpuTimeNs;
            ++gpuSamples;
        }
        sumDrawCalls += stats.drawCallCount;
        sumCommandCount += stats.commandCount;
        sumMergedBatches += stats.mergedBatchCount;
        sumRasterHits += stats.retainedPictureRasterCacheHits;
        sumRasterMisses += stats.retainedPictureRasterCacheMisses;
    }
    const auto benchmarkEnd = std::chrono::steady_clock::now();
    const double totalMs = std::chrono::duration<double, std::milli>(
        benchmarkEnd - benchmarkStart).count();

    // Percentile summary.
    std::vector<double> sorted = frameMs;
    std::sort(sorted.begin(), sorted.end());
    auto pct = [&](double q) {
        if (sorted.empty()) return 0.0;
        const double pos = q * static_cast<double>(sorted.size() - 1);
        const size_t idx = static_cast<size_t>(pos);
        return sorted[idx];
    };
    const double minMs = sorted.front();
    const double maxMs = sorted.back();
    const double p50 = pct(0.50);
    const double p95 = pct(0.95);
    const double p99 = pct(0.99);
    const double avgMs = totalMs / static_cast<double>(measured);
    const double fps = 1000.0 / avgMs;
    const double avgFlushMs = static_cast<double>(sumFlushCpuNs) / static_cast<double>(measured) / 1'000'000.0;
    const double avgGpuMs = gpuSamples > 0
        ? static_cast<double>(sumGpuNs) / static_cast<double>(gpuSamples) / 1'000'000.0
        : 0.0;
    const double avgDraws = static_cast<double>(sumDrawCalls) / static_cast<double>(measured);
    const double avgCommands = static_cast<double>(sumCommandCount) / static_cast<double>(measured);
    const double avgMerged = static_cast<double>(sumMergedBatches) / static_cast<double>(measured);

    char gpuLine[64];
    if (gpuSamples > 0) {
        std::snprintf(gpuLine, sizeof(gpuLine), "%.3f ms", avgGpuMs);
    } else {
        std::snprintf(gpuLine, sizeof(gpuLine), "n/a");
    }

    std::fprintf(stdout,
        "WhatsCanvasDesktopHost benchmark (%dx%d, backend=OpenGL, GL=%s)\n"
        "  frames: warmup=%d, measured=%d, elapsed=%.2f ms\n"
        "  wall-clock frame time (ms):  min=%.3f  p50=%.3f  avg=%.3f  p95=%.3f  p99=%.3f  max=%.3f\n"
        "  throughput: %.1f FPS (%.3f ms/frame avg)\n"
        "  Canvas::getRenderStats() per-frame averages:\n"
        "    flush CPU time:  %.3f ms\n"
        "    GPU time:        %s\n"
        "    draw calls:      %.1f\n"
        "    commands:        %.1f\n"
        "    merged batches:  %.1f\n"
        "    retained Picture raster cache: hits=%zu, misses=%zu (over %d measured frames)\n",
        config.width, config.height,
        wsc::Canvas::getOpenGLVersionString().c_str(),
        warmup, measured, totalMs,
        minMs, p50, avgMs, p95, p99, maxMs,
        fps, avgMs,
        avgFlushMs,
        gpuLine,
        avgDraws, avgCommands, avgMerged,
        sumRasterHits, sumRasterMisses, measured);

    scene.onCanvasReleasing();
    canvas->finalizeContext();
    canvas.reset();
    glfwDestroyWindow(window);
    return 0;
}

} // namespace whatscanvas::desktop
