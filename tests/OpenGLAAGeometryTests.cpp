#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "wsc/Canvas.h"

namespace {

constexpr int kWidth = 96;
constexpr int kHeight = 96;
constexpr std::uint8_t kSourceAlpha = 128;

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "[OpenGLAAGeometryTests] FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    if (!glfwInit()) {
        std::cout << "[OpenGLAAGeometryTests] SKIP: glfwInit failed.\n";
        return 0;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(kWidth, kHeight, "OpenGLAAGeometryTests", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        std::cout << "[OpenGLAAGeometryTests] SKIP: GLFW 3.3 context unavailable.\n";
        return 0;
    }
    glfwMakeContextCurrent(window);
    if (!wsc::Canvas::loadOpenGL(reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return expect(false, "unable to load OpenGL") ? 0 : 1;
    }

    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, kWidth, kHeight);
    if (!canvas || !canvas->initializeContext()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return expect(false, "unable to initialize OpenGL Canvas") ? 0 : 1;
    }
    canvas->setSize(kWidth, kHeight);
    canvas->setDevicePixelRatio(1.5f);
    if (!canvas->setOutputTarget(wsc::OutputTarget::GLFramebuffer(0, kWidth, kHeight, true))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return expect(false, "unable to bind the hidden-window framebuffer") ? 0 : 1;
    }

    wsc::Paint paint;
    paint.setStyle(wsc::Paint::Style::FILL);
    paint.setColor(wsc::Color(255, 255, 255, kSourceAlpha));
    paint.setAntiAlias(true);

    canvas->beginFrame();
    canvas->drawColor(wsc::Color(0, 0, 0, 0));
    canvas->drawRoundRect(wsc::RectF(12.0f, 12.0f, 40.0f, 40.0f), 3.0f, paint);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    const bool read = canvas->readPixelsRGBA(pixels);
    std::uint8_t maximumAlpha = 0;
    std::size_t overCoveredPixels = 0;
    if (read) {
        for (std::size_t index = 3; index < pixels.size(); index += 4) {
            maximumAlpha = std::max(maximumAlpha, pixels[index]);
            if (pixels[index] > kSourceAlpha + 2) {
                ++overCoveredPixels;
            }
        }
    }

    bool ok = expect(read && pixels.size() == static_cast<std::size_t>(kWidth * kHeight * 4),
                     "framebuffer readback must return a complete RGBA frame");
    ok = expect(maximumAlpha <= kSourceAlpha + 2,
                "analytic-AA geometry must not draw its inner fringe over fully covered triangles") && ok;
    ok = expect(overCoveredPixels == 0,
                "a translucent shape must never become more opaque than its source alpha") && ok;
    std::cout << "[OpenGLAAGeometryTests] max alpha " << static_cast<int>(maximumAlpha)
              << ", over-covered pixels " << overCoveredPixels << '\n';

    canvas->setDevicePixelRatio(1.0f);
    paint.setColor(wsc::Color::WHITE);
    auto renderTopologyPair = [&](bool reversed) {
        canvas->beginFrame();
        canvas->drawColor(wsc::Color::BLACK);
        if (!reversed) {
            canvas->drawRect(
                wsc::RectF(8.0f, 8.0f, 20.0f, 20.0f), paint);
            canvas->drawCircle(72.0f, 18.0f, 10.0f, paint);
        } else {
            canvas->drawCircle(18.0f, 72.0f, 10.0f, paint);
            canvas->drawRect(
                wsc::RectF(62.0f, 62.0f, 20.0f, 20.0f), paint);
        }
        canvas->endFrame();
        std::vector<unsigned char> frame;
        ok = expect(
            canvas->readPixelsRGBA(frame),
            "topology cache frame must be readable") && ok;
        return frame;
    };

    const std::vector<unsigned char> topologyA =
        renderTopologyPair(false);
    const std::vector<unsigned char> topologyARepeat =
        renderTopologyPair(false);
    ok = expect(
        topologyA == topologyARepeat,
        "reused shared path topology must render identically") && ok;
    const wsc::Canvas::RenderStats topologyReuseStats =
        canvas->getRenderStats();
    ok = expect(
        topologyReuseStats.pathTopologyCacheHits >= 1
            && topologyReuseStats.pathTopologyCacheMisses == 0,
        "repeated path batches must reuse cached topology") && ok;

    const std::vector<unsigned char> topologyB =
        renderTopologyPair(true);
    const wsc::Canvas::RenderStats topologyChangeStats =
        canvas->getRenderStats();
    ok = expect(
        topologyChangeStats.pathTopologyCacheMisses >= 1,
        "changed path batches must rebuild cached topology") && ok;
    auto redAt = [&](int x, int y) {
        return topologyB[
            (static_cast<std::size_t>(y) * kWidth
             + static_cast<std::size_t>(x))
                * 4u];
    };
    ok = expect(
        topologyB.size()
            == static_cast<std::size_t>(kWidth * kHeight * 4),
        "topology invalidation frame must be readable") && ok;
    if (topologyB.size()
        == static_cast<std::size_t>(kWidth * kHeight * 4)) {
        ok = expect(
            redAt(18, 72) > 250 && redAt(72, 72) > 250,
            "changed shared path topology must rebuild valid indices") && ok;
        ok = expect(
            redAt(18, 18) < 5 && redAt(72, 18) < 5,
            "changed shared path topology must not reuse stale positions") && ok;
    }

    canvas->beginFrame();
    canvas->drawColor(wsc::Color::BLACK);
    for (int index = 0; index < 80; ++index) {
        const int column = index % 10;
        const int row = index / 10;
        paint.setColor(wsc::Color(
            30 + (index * 31) % 210,
            40 + (index * 47) % 200,
            50 + (index * 59) % 190,
            255));
        canvas->drawRect(
            wsc::RectF(
                2.0f + column * 9.0f,
                2.0f + row * 11.0f,
                6.0f, 7.0f),
            paint);
    }
    canvas->endFrame();
    std::vector<unsigned char> parameterizedFrame;
    ok = expect(
        canvas->readPixelsRGBA(parameterizedFrame),
        "shape-parameter batch frame must be readable") && ok;
    const auto expectShapeColor = [&](int index) {
        const int column = index % 10;
        const int row = index / 10;
        const std::size_t offset =
            (static_cast<std::size_t>(5 + row * 11) * kWidth
             + static_cast<std::size_t>(5 + column * 9))
            * 4u;
        const int expected[3] = {
            30 + (index * 31) % 210,
            40 + (index * 47) % 200,
            50 + (index * 59) % 190
        };
        return parameterizedFrame.size()
                == static_cast<std::size_t>(kWidth * kHeight * 4)
            && std::abs(
                static_cast<int>(parameterizedFrame[offset])
                - expected[0]) <= 2
            && std::abs(
                static_cast<int>(parameterizedFrame[offset + 1u])
                - expected[1]) <= 2
            && std::abs(
                static_cast<int>(parameterizedFrame[offset + 2u])
                - expected[2]) <= 2;
    };
    ok = expect(
        expectShapeColor(0)
            && expectShapeColor(37)
            && expectShapeColor(79),
        "GPU shape parameters must preserve per-shape transforms and colors") && ok;

    canvas->beginFrame();
    canvas->drawColor(wsc::Color::BLACK);
    paint.setAntiAlias(false);
    for (int index = 0; index < 16; ++index) {
        const int column = index % 4;
        const int row = index / 4;
        if ((index % 2) == 0) {
            paint.setBlendMode(wsc::Paint::BlendMode::ADD);
            paint.setColor(wsc::Color(180, 20, 10, 255));
        } else {
            paint.setBlendMode(wsc::Paint::BlendMode::SCREEN);
            paint.setColor(wsc::Color(10, 180, 20, 255));
        }
        canvas->drawRect(
            wsc::RectF(
                5.0f + column * 22.0f,
                5.0f + row * 22.0f,
                12.0f, 12.0f),
            paint);
    }
    canvas->endFrame();
    std::vector<unsigned char> independentFrame;
    ok = expect(
        canvas->readPixelsRGBA(independentFrame),
        "independent blend batch frame must be readable") && ok;
    const wsc::Canvas::RenderStats independentStats =
        canvas->getRenderStats();
    ok = expect(
        independentStats.drawCallCount <= 3,
        "non-overlapping paths should submit once per blend state") && ok;
    ok = expect(
        independentStats.mergedBatchCount >= 2,
        "non-overlapping blend groups should use path batches") && ok;
    if (independentFrame.size()
        == static_cast<std::size_t>(kWidth * kHeight * 4)) {
        const auto channelAt = [&](int x, int y, std::size_t channel) {
            return independentFrame[
                (static_cast<std::size_t>(y) * kWidth
                 + static_cast<std::size_t>(x)) * 4u
                + channel];
        };
        ok = expect(
            channelAt(10, 10, 0) > 170
                && channelAt(32, 10, 1) > 170,
            "blend grouping must preserve each independent path result") && ok;
    }

    canvas->beginFrame();
    canvas->drawColor(wsc::Color::BLACK);
    for (int index = 0; index < 16; ++index) {
        paint.setBlendMode(
            (index % 2) == 0
                ? wsc::Paint::BlendMode::ADD
                : wsc::Paint::BlendMode::SCREEN);
        paint.setColor(
            (index % 2) == 0
                ? wsc::Color(12, 6, 3, 255)
                : wsc::Color(3, 12, 6, 255));
        canvas->drawRect(
            wsc::RectF(20.0f, 20.0f, 30.0f, 30.0f), paint);
    }
    canvas->endFrame();
    const wsc::Canvas::RenderStats overlappingStats =
        canvas->getRenderStats();
    ok = expect(
        overlappingStats.drawCallCount >= 16,
        "overlapping blend paths must remain strict order barriers") && ok;

    canvas.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return ok ? 0 : 1;
}
