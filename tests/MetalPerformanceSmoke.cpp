#include <wsc/CanvasStats.h>

// WhatsCanvas cross-backend performance smoke (Software / OpenGL / Metal /
// Vulkan). Renders the same workload through every available backend, reports
// wall-clock time per frame, and — when the backend supports it — the GPU-side
// frame time via Canvas::getRenderStats().gpuTimeNs.
//
// OpenGL requires a live GL context, so this smoke opens a hidden GLFW window
// with a core-profile context and calls Canvas::loadOpenGL against
// glfwGetProcAddress before the Canvas is created. Metal, Vulkan, and Software
// all work headless.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "wsc/wsc.h"

using namespace wsc;

namespace {

const char *backendName(Canvas::Backend b)
{
    switch (b) {
    case Canvas::Backend::OpenGL: return "OpenGL";
    case Canvas::Backend::Metal:  return "Metal";
    case Canvas::Backend::Vulkan: return "Vulkan";
    case Canvas::Backend::Software: return "Software";
    default: return "?";
    }
}

struct BackendResult
{
    Canvas::Backend backend = Canvas::Backend::Software;
    bool ok = false;
    double totalSeconds = 0.0;
    double firstFrameSeconds = 0.0;
    std::uint64_t lastGpuTimeNs = 0;
    bool gpuTimingAvailable = false;
    int frames = 0;
};

void drawWorkload(Canvas &canvas, int frame, int rects)
{
    const int w = canvas.getWidth();
    const int h = canvas.getHeight();
    canvas.beginFrame();
    canvas.drawColor(Color(24, 26, 34));
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setAntiAlias(false);
    for (int i = 0; i < rects; ++i) {
        fill.setColor(Color((i * 5 + frame) & 0xff, (i * 11) & 0xff, (i * 17) & 0xff, 255));
        const int step = i * 13 + frame;
        const float x = static_cast<float>(step % (w - 24));
        const float y = static_cast<float>((i * 17 + frame) % (h - 24));
        canvas.drawRect(RectF(x, y, x + 24.0f, y + 24.0f), fill);
    }
    canvas.endFrame();
}

BackendResult runBackend(Canvas::Backend backend, int frames, int rects,
                         int width, int height)
{
    BackendResult r;
    r.backend = backend;
    if (!Canvas::isBackendAvailable(backend)) {
        return r;
    }
    auto canvas = Canvas::create(backend, width, height);
    if (!canvas) {
        return r;
    }
    canvas->initializeContext();
    canvas->setGpuTimingEnabled(true);

    // Warm up so the first-frame pipeline compiles / cache misses don't bias
    // the sustained numbers.
    drawWorkload(*canvas, 0, rects);
    std::vector<unsigned char> pixels;
    canvas->readPixelsRGBA(pixels);

    // Time the first "real" frame separately so callers can spot cold-start
    // penalties independently of steady state.
    auto tFirst0 = std::chrono::steady_clock::now();
    drawWorkload(*canvas, 1, rects);
    canvas->readPixelsRGBA(pixels);
    auto tFirst1 = std::chrono::steady_clock::now();
    r.firstFrameSeconds = std::chrono::duration<double>(tFirst1 - tFirst0).count();

    auto t0 = std::chrono::steady_clock::now();
    for (int frame = 2; frame < frames; ++frame) {
        drawWorkload(*canvas, frame, rects);
        canvas->readPixelsRGBA(pixels);
    }
    auto t1 = std::chrono::steady_clock::now();
    r.totalSeconds = std::chrono::duration<double>(t1 - t0).count();
    r.frames = std::max(0, frames - 2);
    r.ok = true;

    auto stats = canvas->getRenderStats();
    r.gpuTimingAvailable = stats.gpuTimeAvailable;
    r.lastGpuTimeNs = stats.gpuTimeNs;
    return r;
}

bool ensureHiddenGlfwGlContext(GLFWwindow *&outWindow)
{
    if (!glfwInit()) {
        return false;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    outWindow = glfwCreateWindow(128, 128, "wsc-perf-headless", nullptr, nullptr);
    if (!outWindow) {
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(outWindow);
    return Canvas::loadOpenGL(reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress));
}

void printResult(const BackendResult &r, int rects, int width, int height)
{
    if (!r.ok) {
        std::printf("%-8s : unavailable\n", backendName(r.backend));
        return;
    }
    const double frameMs = r.frames > 0 ? (r.totalSeconds * 1000.0 / r.frames) : 0.0;
    std::printf("%-8s : %.3f ms/frame (first %.3f ms) — %dx%d, %d rects, %d frames",
                backendName(r.backend), frameMs, r.firstFrameSeconds * 1000.0,
                width, height, rects, r.frames);
    if (r.gpuTimingAvailable) {
        std::printf(", GPU %.3f ms", r.lastGpuTimeNs / 1'000'000.0);
    }
    std::printf("\n");
}

} // namespace

int main()
{
    const int frames = 200;
    const int rects = 128;
    const int width = 256;
    const int height = 256;

    GLFWwindow *glWindow = nullptr;
    bool haveGlContext = ensureHiddenGlfwGlContext(glWindow);
    if (!haveGlContext) {
        std::cerr << "[MetalPerf] Could not create a hidden GL context; OpenGL will be skipped."
                  << std::endl;
    }

    const Canvas::Backend backends[] = {
        Canvas::Backend::Software,
        Canvas::Backend::OpenGL,
        Canvas::Backend::Metal,
        Canvas::Backend::Vulkan,
    };

    std::vector<BackendResult> results;
    for (Canvas::Backend b : backends) {
        if (b == Canvas::Backend::OpenGL && !haveGlContext) {
            std::printf("%-8s : skipped (no GL context)\n", backendName(b));
            continue;
        }
        auto r = runBackend(b, frames, rects, width, height);
        printResult(r, rects, width, height);
        results.push_back(r);
    }

    // Print pairwise Metal vs OpenGL / Metal vs Software ratios when both
    // sides have valid data.
    auto findResult = [&](Canvas::Backend b) -> const BackendResult * {
        for (const BackendResult &r : results) {
            if (r.backend == b && r.ok) return &r;
        }
        return nullptr;
    };
    const auto *m = findResult(Canvas::Backend::Metal);
    const auto *gl = findResult(Canvas::Backend::OpenGL);
    const auto *sw = findResult(Canvas::Backend::Software);
    if (m != nullptr && gl != nullptr) {
        std::printf("Metal / OpenGL ratio (lower = Metal faster): %.2fx wall-clock\n",
                    m->totalSeconds / gl->totalSeconds);
    }
    if (m != nullptr && sw != nullptr) {
        std::printf("Metal / Software ratio (lower = Metal faster): %.2fx wall-clock\n",
                    m->totalSeconds / sw->totalSeconds);
    }

    if (glWindow != nullptr) {
        glfwDestroyWindow(glWindow);
        glfwTerminate();
    }
    return 0;
}
