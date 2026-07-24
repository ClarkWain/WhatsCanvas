#include <algorithm>
#include <cstdint>
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

    canvas.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return ok ? 0 : 1;
}
