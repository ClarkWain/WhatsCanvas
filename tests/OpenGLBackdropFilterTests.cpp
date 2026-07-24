#include <cstdint>
#include <iostream>
#include <vector>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "wsc/wsc.h"

namespace {

constexpr int kWidth = 40;
constexpr int kHeight = 24;

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "[OpenGLBackdropFilterTests] FAIL: " << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    if (!glfwInit()) {
        std::cout << "[OpenGLBackdropFilterTests] SKIP: glfwInit failed.\n";
        return 0;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(kWidth, kHeight, "OpenGLBackdropFilterTests", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        std::cout << "[OpenGLBackdropFilterTests] SKIP: GLFW 3.3 context unavailable.\n";
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
    if (!canvas->setOutputTarget(wsc::OutputTarget::GLFramebuffer(0, kWidth, kHeight, true))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return expect(false, "unable to bind hidden-window framebuffer") ? 0 : 1;
    }
    GLint outputFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &outputFramebuffer);

    wsc::Paint black;
    black.setStyle(wsc::Paint::Style::FILL);
    black.setColor(wsc::Color(0, 0, 0, 255));
    black.setAntiAlias(false);
    wsc::Paint white = black;
    white.setColor(wsc::Color(255, 255, 255, 255));
    wsc::Paint layerPaint;
    layerPaint.setColor(wsc::Color(255, 255, 255, 255));
    wsc::LayerOptions options;
    options.setBackdropFilter(wsc::ImageFilter::blur(6.0f));

    canvas->beginFrame();
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, 20.0f, static_cast<float>(kHeight)), black);
    canvas->drawRect(wsc::RectF(20.0f, 0.0f, 20.0f, static_cast<float>(kHeight)), white);
    canvas->saveLayer(wsc::RectF(8.0f, 4.0f, 24.0f, 16.0f), layerPaint, options);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    GLint framebufferAfterBackdrop = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebufferAfterBackdrop);
    bool ok = expect(framebufferAfterBackdrop == outputFramebuffer,
                     "backdrop filtering must restore the output framebuffer");
    const bool read = canvas->readPixelsRGBA(pixels);
    auto pixelAt = [&](int x, int y) {
        return &pixels[(static_cast<std::size_t>(y) * kWidth + x) * 4u];
    };

    ok = expect(read && pixels.size() == static_cast<std::size_t>(kWidth * kHeight * 4),
                "framebuffer readback must return a complete RGBA frame") && ok;
    if (ok) {
        ok = expect(pixelAt(6, 12)[0] < 5, "black pixels outside the layer should remain sharp") && ok;
        ok = expect(pixelAt(34, 12)[0] > 250, "white pixels outside the layer should remain sharp") && ok;
        ok = expect(pixelAt(18, 12)[0] > 0 && pixelAt(18, 12)[0] < 128,
                    "blur should spread white into the black half") && ok;
        ok = expect(pixelAt(21, 12)[0] > 128 && pixelAt(21, 12)[0] < 255,
                    "blur should spread black into the white half") && ok;
    }

    canvas->beginFrame();
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, 20.0f, static_cast<float>(kHeight)), black);
    canvas->drawRect(wsc::RectF(20.0f, 0.0f, 20.0f, static_cast<float>(kHeight)), white);
    canvas->saveLayer(wsc::RectF(8.0f, 4.0f, 24.0f, 16.0f), layerPaint, options);
    wsc::Path roundedClip;
    roundedClip.addRoundRect(wsc::RectF(8.0f, 4.0f, 24.0f, 16.0f), 5.0f);
    canvas->clipPath(roundedClip);
    wsc::Paint tint = white;
    tint.setColor(wsc::Color(80, 160, 220, 64));
    canvas->drawRect(wsc::RectF(8.0f, 4.0f, 24.0f, 16.0f), tint);
    canvas->restore();
    canvas->endFrame();

    pixels.clear();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebufferAfterBackdrop);
    ok = expect(framebufferAfterBackdrop == outputFramebuffer,
                "rounded backdrop filtering must restore the output framebuffer") && ok;
    ok = expect(canvas->readPixelsRGBA(pixels),
                "rounded backdrop readback should succeed") && ok;
    if (ok) {
        ok = expect(pixelAt(3, 12)[0] < 5,
                    "rounded backdrop must preserve black pixels outside its layer") && ok;
        ok = expect(pixelAt(36, 12)[0] > 250,
                    "rounded backdrop must preserve white pixels outside its layer") && ok;
        ok = expect(pixelAt(18, 12)[0] > 0 && pixelAt(18, 12)[0] < 160,
                    "rounded backdrop should retain the blurred transition") && ok;
    }

    canvas.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return ok ? 0 : 1;
}
