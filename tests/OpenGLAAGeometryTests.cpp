#include <wsc/CanvasStats.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "wsc/Canvas.h"
#include "wsc/Path.h"

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

    // Direct open-arc submission must remain pixel-identical to the generic
    // Path replay retained by Picture recording. This protects the semantic
    // fast path's sampling, caps and analytic-AA fringe.
    wsc::Paint arcPaint;
    arcPaint.setStyle(wsc::Paint::Style::STROKE);
    arcPaint.setStrokeColor(wsc::Color(72, 224, 198, 220));
    arcPaint.setStrokeWidth(9.0f);
    arcPaint.setStrokeCap(wsc::Paint::StrokeCap::ROUND);
    arcPaint.setAntiAlias(true);
    const wsc::RectF arcBounds(17.0f, 18.0f, 62.0f, 55.0f);
    constexpr float arcStart = -2.35f;
    constexpr float arcSweep = 4.25f;
    const auto arcPicture = canvas->recordPicture(
        [&](wsc::Canvas &recording) {
            recording.drawArc(
                arcBounds, arcStart, arcSweep,
                wsc::Canvas::ArcMode::OPEN, arcPaint);
        });
    auto renderArc = [&](bool pictureReplay) {
        canvas->beginFrame();
        canvas->drawColor(wsc::Color(8, 12, 29, 255));
        if (pictureReplay && arcPicture) {
            canvas->drawPicture(*arcPicture);
        } else {
            canvas->drawArc(
                arcBounds, arcStart, arcSweep,
                wsc::Canvas::ArcMode::OPEN, arcPaint);
        }
        canvas->endFrame();
        std::vector<unsigned char> frame;
        ok = expect(
            canvas->readPixelsRGBA(frame),
            "open-arc comparison frame must be readable") && ok;
        return frame;
    };
    const std::vector<unsigned char> directArc = renderArc(false);
    const std::vector<unsigned char> pictureArc = renderArc(true);
    ok = expect(
        arcPicture && directArc == pictureArc,
        "direct open-arc stroke must match generic Picture path replay") && ok;

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

    // A modal dialog introduces blurred rounded-rectangle shadows between
    // ordinary indexed path batches. This used to leave DrawPath trusting a
    // stale element-buffer cache; NVIDIA then interpreted the non-zero index
    // offset as a client pointer inside glDrawElements.
    canvas->beginFrame();
    canvas->drawColor(wsc::Color(246, 241, 234, 255));
    paint.setBlendMode(wsc::Paint::BlendMode::SRC_OVER);
    paint.setAntiAlias(true);
    paint.setColor(wsc::Color(0, 0, 0, 0));
    paint.setShadowLayer(18.0f, 0.0f, 8.0f,
                         wsc::Color(0, 0, 0, 64));
    canvas->drawRoundRect(
        wsc::RectF(14.0f, 18.0f, 68.0f, 58.0f), 12.0f, paint);
    paint.clearShadowLayer();
    paint.setColor(wsc::Color(255, 252, 248, 255));
    canvas->drawRoundRect(
        wsc::RectF(14.0f, 18.0f, 68.0f, 58.0f), 12.0f, paint);
    for (int row = 0; row < 4; ++row) {
        paint.setColor(wsc::Color(
            220 - row * 12, 72 + row * 18, 62 + row * 8, 255));
        canvas->drawRoundRect(
            wsc::RectF(23.0f, 29.0f + row * 10.0f,
                       50.0f, 6.0f), 3.0f, paint);
    }
    canvas->endFrame();
    std::vector<unsigned char> modalFrame;
    ok = expect(
        canvas->readPixelsRGBA(modalFrame)
            && modalFrame.size()
                == static_cast<std::size_t>(kWidth * kHeight * 4),
        "modal shadow followed by indexed paths must render without corrupting GL bindings") && ok;

    // Initialize every lazy context-bound program that participates in the
    // Android demo's clipped shadows. The same effect is rendered again after
    // replacing the native GL context below.
    auto renderContextBoundEffects = [&]() {
        canvas->beginFrame();
        canvas->drawColor(wsc::Color::BLACK);
        wsc::Path circleClip;
        circleClip.addCircle(48.0f, 48.0f, 30.0f);
        canvas->save();
        canvas->clipPath(circleClip);
        paint.setBlendMode(wsc::Paint::BlendMode::SRC_OVER);
        paint.setAntiAlias(true);
        paint.setColor(wsc::Color(48, 210, 184, 255));
        paint.setShadowLayer(14.0f, 5.0f, 4.0f,
                             wsc::Color(255, 92, 72, 180));
        canvas->drawRoundRect(
            wsc::RectF(24.0f, 30.0f, 48.0f, 36.0f), 9.0f, paint);
        paint.clearShadowLayer();
        canvas->restore();
        canvas->endFrame();

        std::vector<unsigned char> frame;
        const bool complete = canvas->readPixelsRGBA(frame)
            && frame.size() == static_cast<std::size_t>(kWidth * kHeight * 4);
        ok = expect(complete,
                    "clipped shadow frame must be readable") && ok;
        if (complete) {
            const std::size_t center =
                (static_cast<std::size_t>(48) * kWidth + 48u) * 4u;
            ok = expect(frame[center + 1u] > 180 && frame[center + 2u] > 140,
                        "clipped shadow frame must preserve its center fill") && ok;
        }
        return frame;
    };

    const std::vector<unsigned char> effectsBeforeContextLoss =
        renderContextBoundEffects();

    // Reproduce Android's orderly pause/resume contract: finalize while the old
    // context is current, destroy that context, then construct a Canvas in a
    // fresh context. Lazy GL singletons must participate in finalization so the
    // new Canvas recreates its FBOs, textures, VAOs and programs.
    canvas->finalizeContext();
    canvas.reset();
    glfwDestroyWindow(window);
    window = glfwCreateWindow(kWidth, kHeight,
                              "OpenGLAAGeometryTests-recreated", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return expect(false, "unable to recreate the OpenGL context") ? 0 : 1;
    }
    glfwMakeContextCurrent(window);

    canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, kWidth, kHeight);
    ok = expect(canvas && canvas->initializeContext(),
                "Canvas must initialize after native GL context replacement") && ok;
    if (canvas) {
        canvas->setSize(kWidth, kHeight);
        canvas->setDevicePixelRatio(1.0f);
        ok = expect(
            canvas->setOutputTarget(
                wsc::OutputTarget::GLFramebuffer(0, kWidth, kHeight, true)),
            "recreated Canvas must bind the new default framebuffer") && ok;
        const std::vector<unsigned char> effectsAfterContextLoss =
            renderContextBoundEffects();
        ok = expect(effectsAfterContextLoss == effectsBeforeContextLoss,
                    "context replacement must recreate clipped-shadow resources exactly") && ok;
        ok = expect(glGetError() == GL_NO_ERROR,
                    "context replacement must not leave an OpenGL error") && ok;
    }

    canvas.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return ok ? 0 : 1;
}
