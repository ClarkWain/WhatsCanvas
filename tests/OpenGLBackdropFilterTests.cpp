#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "wsc/wsc.h"

namespace {

constexpr int kWidth = 160;
constexpr int kHeight = 128;

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

    wsc::LayerOptions downsampleOptions;
    downsampleOptions.setBackdropFilter(wsc::ImageFilter::blur(32.0f));
    canvas->beginFrame();
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, 80.0f, static_cast<float>(kHeight)), black);
    canvas->drawRect(wsc::RectF(80.0f, 0.0f, 80.0f, static_cast<float>(kHeight)), white);
    canvas->saveLayer(wsc::RectF(0.0f, 0.0f, static_cast<float>(kWidth),
                                 static_cast<float>(kHeight)),
                      layerPaint, downsampleOptions);
    canvas->restore();
    canvas->endFrame();

    pixels.clear();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebufferAfterBackdrop);
    ok = expect(framebufferAfterBackdrop == outputFramebuffer,
                "downsampled filtering must restore the output framebuffer") && ok;
    ok = expect(canvas->readPixelsRGBA(pixels),
                "downsampled backdrop readback should succeed") && ok;
    if (ok) {
        ok = expect(pixelAt(70, 64)[0] > 0 && pixelAt(70, 64)[0] < 128,
                    "downsampled blur should spread white into the black half") && ok;
        ok = expect(pixelAt(89, 64)[0] > 128 && pixelAt(89, 64)[0] < 255,
                    "downsampled blur should spread black into the white half") && ok;
        ok = expect(pixelAt(70, 64)[3] == 255 && pixelAt(89, 64)[3] == 255,
                    "full-resolution restore pass should preserve opaque alpha") && ok;
        const wsc::Canvas::RenderStats stats = canvas->getRenderStats();
        ok = expect(stats.filterCount == 1 && stats.filterPassCount == 3
                        && stats.downsampledFilterCount == 1,
                    "GPU stats should report one downsampled three-pass filter") && ok;
        ok = expect(stats.filterInputPixelCount == 20480
                        && stats.filterPixelPassCount == 30720,
                    "GPU stats should report input and reduced pixel-pass work") && ok;
    }

    canvas->beginFrame();
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, static_cast<float>(kWidth),
                                static_cast<float>(kHeight)), black);
    canvas->drawRect(wsc::RectF(6.0f, 0.0f, 2.0f, static_cast<float>(kHeight)), white);
    canvas->saveLayer(wsc::RectF(8.0f, 4.0f, 24.0f, 16.0f), layerPaint, options);
    canvas->restore();
    canvas->endFrame();

    pixels.clear();
    ok = expect(canvas->readPixelsRGBA(pixels),
                "bounded backdrop readback should succeed") && ok;
    if (ok) {
        ok = expect(pixelAt(5, 12)[0] < 5,
                    "backdrop sampling outset must not darken pixels outside the layer") && ok;
        ok = expect(pixelAt(7, 12)[0] > 250,
                    "backdrop sampling outset must not overwrite source pixels outside the layer") && ok;
        ok = expect(pixelAt(9, 12)[0] > 0,
                    "pixels outside the layer should still contribute to blur inside its boundary") && ok;
    }

    canvas->beginFrame();
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, 20.0f, static_cast<float>(kHeight)), black);
    canvas->drawRect(wsc::RectF(20.0f, 0.0f, 20.0f, static_cast<float>(kHeight)), white);
    canvas->save();
    wsc::Path roundedClip;
    roundedClip.addRoundRect(wsc::RectF(8.0f, 4.0f, 24.0f, 16.0f), 5.0f);
    canvas->clipPath(roundedClip);
    canvas->saveLayer(wsc::RectF(8.0f, 4.0f, 24.0f, 16.0f), layerPaint, options);
    wsc::Paint transparentGradient = white;
    transparentGradient.setLinearGradient(
        8.0f, 4.0f, 32.0f, 20.0f,
        wsc::Color(255, 80, 120, 0), wsc::Color(80, 180, 255, 0));
    canvas->drawRect(wsc::RectF(8.0f, 4.0f, 24.0f, 16.0f), transparentGradient);
    canvas->restore();
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

    wsc::Paint colorBackground = black;
    colorBackground.setColor(wsc::Color(240, 80, 40, 255));
    wsc::ImageFilter grayscale = wsc::ImageFilter::blur(1.0f);
    grayscale.setColorAdjustment(0.0f);
    wsc::LayerOptions grayscaleOptions;
    grayscaleOptions.setBackdropFilter(grayscale);
    canvas->beginFrame();
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, static_cast<float>(kWidth),
                                static_cast<float>(kHeight)), colorBackground);
    canvas->saveLayer(wsc::RectF(0.0f, 0.0f, static_cast<float>(kWidth),
                                 static_cast<float>(kHeight)),
                      layerPaint, grayscaleOptions);
    canvas->restore();
    canvas->endFrame();

    pixels.clear();
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebufferAfterBackdrop);
    ok = expect(framebufferAfterBackdrop == outputFramebuffer,
                "color-adjusted backdrop must restore the output framebuffer") && ok;
    ok = expect(canvas->readPixelsRGBA(pixels),
                "color-adjusted backdrop readback should succeed") && ok;
    if (ok) {
        const unsigned char *center = pixelAt(kWidth / 2, kHeight / 2);
        const int rg = std::abs(static_cast<int>(center[0]) - static_cast<int>(center[1]));
        const int gb = std::abs(static_cast<int>(center[1]) - static_cast<int>(center[2]));
        ok = expect(rg <= 2 && gb <= 2,
                    "GPU zero saturation should turn the filtered backdrop grayscale") && ok;
        ok = expect(center[3] == 255,
                    "GPU color adjustment should preserve backdrop alpha") && ok;
    }

    canvas->beginFrame();
    for (int x = 0; x < kWidth; x += 4) {
        canvas->drawRect(wsc::RectF(static_cast<float>(x), 0.0f, 4.0f,
                                    static_cast<float>(kHeight)),
                         ((x / 4) % 2) == 0 ? black : white);
    }
    auto drawRoundedBackdrop = [&](const wsc::RectF &bounds) {
        canvas->save();
        wsc::Path clip;
        clip.addRoundRect(bounds, 4.0f);
        canvas->clipPath(clip);
        canvas->saveLayer(bounds, layerPaint, options);
        canvas->restore();
        canvas->restore();
    };
    drawRoundedBackdrop(wsc::RectF(2.0f, 4.0f, 16.0f, 16.0f));
    drawRoundedBackdrop(wsc::RectF(22.0f, 4.0f, 16.0f, 16.0f));
    canvas->endFrame();

    pixels.clear();
    ok = expect(canvas->readPixelsRGBA(pixels),
                "multiple rounded backdrop readback should succeed") && ok;
    if (ok) {
        ok = expect(pixelAt(8, 12)[0] > 0 && pixelAt(8, 12)[0] < 255,
                    "first independent rounded backdrop should blur striped content") && ok;
        ok = expect(pixelAt(24, 12)[0] > 0 && pixelAt(24, 12)[0] < 255,
                    "second rounded backdrop should replay an earlier clipped layer") && ok;
        ok = expect(pixelAt(20, 2)[0] == 255,
                    "pixels outside rounded backdrop layers should remain sharp") && ok;
    }

    canvas.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return ok ? 0 : 1;
}
