#include <wsc/CanvasStats.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "support/CompositeFilterParityScene.h"
#include "support/PixelParity.h"

namespace {

using whatscanvas::test::kCompositeParityHeight;
using whatscanvas::test::kCompositeParityWidth;

#if defined(WHATSCANVAS_PARITY_OPENGLES)
constexpr const char *kBackendName = "opengles";
constexpr wsc::Canvas::Backend kBackend = wsc::Canvas::Backend::OpenGLES;
// Mesa's GLES path has stable 3-5 LSB rounding at filtered layer boundaries.
// Keep the mean-error limit strict while allowing that sub-percent edge band.
constexpr int kMaxChannelDifference = 5;
constexpr double kMaxBadPixelRatio = 0.007;
#else
constexpr const char *kBackendName = "opengl";
constexpr wsc::Canvas::Backend kBackend = wsc::Canvas::Backend::OpenGL;
constexpr int kMaxChannelDifference = 4;
constexpr double kMaxBadPixelRatio = 0.005;
#endif

#if !defined(WHATSCANVAS_PARITY_OPENGLES)
constexpr GLenum kDebugOutput = 0x92E0;
constexpr GLenum kDebugOutputSynchronous = 0x8242;
constexpr GLenum kDebugTypeError = 0x824C;

using GLDebugCallback = void (APIENTRY *)(
    GLenum, GLenum, GLuint, GLenum, GLsizei, const GLchar *, const void *);
using GLDebugMessageCallbackProc = void (APIENTRY *)(
    GLDebugCallback, const void *);

void APIENTRY reportOpenGLDebugMessage(
    GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei length, const GLchar *message, const void *)
{
    if (type != kDebugTypeError) {
        return;
    }
    std::cerr << "FILTER_PARITY_GL_DEBUG"
              << " source=" << source
              << " type=" << type
              << " id=" << id
              << " severity=" << severity
              << " message="
              << std::string(message, static_cast<std::size_t>(length))
              << '\n';
}
#endif

bool contextIsRequired()
{
    const char *value = std::getenv("WHATSCANVAS_REQUIRE_GL_CONTEXT");
    return value != nullptr && std::string(value) != "0";
}

int unavailable(const char *reason)
{
    std::cerr << "FILTER_PARITY backend=" << kBackendName
              << " status=" << (contextIsRequired() ? "FAIL" : "SKIP")
              << " reason=" << reason << '\n';
    return contextIsRequired() ? 1 : 0;
}

void reportPixelSample(
    const char *label,
    const std::vector<unsigned char> &pixels,
    int x, int y)
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * kCompositeParityWidth
         + static_cast<std::size_t>(x)) * 4u;
    if (offset + 3u >= pixels.size()) {
        return;
    }
    std::cerr << "FILTER_PARITY_SAMPLE"
              << " backend=" << kBackendName
              << " image=" << label
              << " x=" << x
              << " y=" << y
              << " rgba="
              << static_cast<int>(pixels[offset + 0u]) << ","
              << static_cast<int>(pixels[offset + 1u]) << ","
              << static_cast<int>(pixels[offset + 2u]) << ","
              << static_cast<int>(pixels[offset + 3u]) << '\n';
}

struct GLTarget
{
    GLuint framebuffer = 0;
    GLuint color = 0;
    GLuint depthStencil = 0;

    bool create()
    {
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kCompositeParityWidth,
                     kCompositeParityHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);

        glGenRenderbuffers(1, &depthStencil);
        glBindRenderbuffer(GL_RENDERBUFFER, depthStencil);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                              kCompositeParityWidth, kCompositeParityHeight);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, depthStencil);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }

    void destroy()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (depthStencil != 0) {
            glDeleteRenderbuffers(1, &depthStencil);
        }
        if (color != 0) {
            glDeleteTextures(1, &color);
        }
        if (framebuffer != 0) {
            glDeleteFramebuffers(1, &framebuffer);
        }
    }
};

// Analytic colors on a uniform background isolate clip-coordinate failures
// from differences between CPU/GPU blur kernels. This runs in both the GL and
// GLES parity gates, where CI requires a real context instead of accepting SKIP.
bool checkClipMaskOffsets(wsc::Canvas &canvas)
{
    wsc::Paint background;
    background.setColor(wsc::Color(20, 40, 60, 255));
    wsc::Paint composite;
    composite.setColor(wsc::Color(255, 255, 255, 255));
    wsc::LayerOptions blur;
    blur.setBackdropFilter(wsc::ImageFilter::blur(3.0f));
    const wsc::Color colors[] = {
        wsc::Color(220, 40, 60, 128), wsc::Color(20, 220, 60, 128)
    };
    const unsigned char imagePixels[2][4] = {
        {220, 40, 60, 128}, {20, 220, 60, 128}
    };
    wsc::Image images[2];
    for (int i = 0; i < 2; ++i) {
        if (!canvas.loadImageFromRGBA(images[i], imagePixels[i], 1, 1)) {
            return false;
        }
    }
    const wsc::RectF panels[] = {
        wsc::RectF(24, 18, 104, 68), wsc::RectF(80, 48, 88, 64)
    };
    std::vector<unsigned char> pixels;
    bool passed = true;
    int checks = 0;
    auto read = [&]() {
        pixels.clear();
        const bool ok = canvas.readPixelsRGBA(pixels)
            && pixels.size() == kCompositeParityWidth * kCompositeParityHeight * 4u;
        if (!ok) {
            std::cerr << "CLIP_OFFSET_REGRESSION backend=" << kBackendName
                      << " status=FAIL reason=readback\n";
        }
        return ok;
    };
    auto check = [&](const char *scene, int x, int y, int r, int g, int b) {
        ++checks;
        const auto *pixel = &pixels[(y * kCompositeParityWidth + x) * 4u];
        if (std::abs(int(pixel[0]) - r) > 3
            || std::abs(int(pixel[1]) - g) > 3
            || std::abs(int(pixel[2]) - b) > 3 || pixel[3] != 255) {
            std::cerr << "CLIP_OFFSET_REGRESSION backend=" << kBackendName
                      << " status=FAIL scene=" << scene << " x=" << x << " y=" << y
                      << " actual=" << int(pixel[0]) << ',' << int(pixel[1])
                      << ',' << int(pixel[2]) << ',' << int(pixel[3])
                      << " expected=" << r << ',' << g << ',' << b << ",255\n";
            passed = false;
        }
    };
    auto clip = [&](const wsc::RectF &bounds) {
        wsc::Path path;
        path.addRoundRect(bounds, 10.0f);
        canvas.clipPath(path);
    };
    auto drawOnMainTarget = [&]() {
        canvas.save();
        const wsc::RectF bounds(4, 98, 20, 26);
        clip(bounds);
        wsc::Paint paint;
        paint.setColor(wsc::Color(10, 210, 30, 255));
        canvas.drawRect(bounds, paint);
        canvas.restore();
    };

    // Exercise filled paths, gradient paths and sampled images. Each later
    // backdrop also replays the earlier clipped composite at a new offset.
    for (int mode = 0; mode < 3; ++mode) {
        const char *scene = mode == 0 ? "solid" : mode == 1 ? "gradient" : "image";
        canvas.beginFrame();
        canvas.drawRect(wsc::RectF(0, 0, kCompositeParityWidth, kCompositeParityHeight), background);
        for (int i = 0; i < 2; ++i) {
            canvas.save();
            clip(panels[i]);
            canvas.saveLayer(panels[i], composite, blur);
            wsc::Paint tint;
            tint.setColor(colors[i]);
            if (mode == 2) {
                canvas.drawImage(images[i], panels[i], composite);
            } else {
                if (mode == 1) {
                    tint.setLinearGradient(0, 0, 192, 128, colors[i], colors[i]);
                }
                canvas.drawRect(panels[i], tint);
            }
            canvas.restore();
            canvas.restore();
        }
        drawOnMainTarget();
        canvas.endFrame();
        if (!read()) return false;
        check(scene, 40, 36, 120, 40, 60);
        check(scene, 60, 68, 120, 40, 60);
        check(scene, 100, 65, 70, 130, 60);
        check(scene, 150, 96, 20, 130, 60);
        check(scene, 24, 18, 20, 40, 60); // Outside the first rounded corner.
        check(scene, 80, 48, 120, 40, 60); // Second corner preserves first panel.
        check(scene, 180, 120, 20, 40, 60);
        check(scene, 14, 111, 10, 210, 30); // Offset must return to zero.
    }

    // Nested layers use absolute canvas coordinates, not accumulated offsets.
    canvas.beginFrame();
    canvas.drawRect(wsc::RectF(0, 0, kCompositeParityWidth, kCompositeParityHeight), background);
    canvas.save();
    const wsc::RectF outer(44, 26, 100, 76);
    clip(outer);
    canvas.saveLayer(outer, composite);
    wsc::Paint red;
    red.setColor(wsc::Color(220, 40, 60, 255));
    canvas.drawRect(outer, red);
    canvas.save();
    const wsc::RectF inner(62, 44, 52, 34);
    clip(inner);
    canvas.saveLayer(inner, composite);
    canvas.drawImage(images[1], inner, composite);
    canvas.restore();
    canvas.restore();
    canvas.restore();
    canvas.restore();
    drawOnMainTarget();
    canvas.endFrame();
    if (!read()) return false;
    check("nested", 88, 61, 120, 130, 60);
    check("nested", 54, 64, 220, 40, 60);
    check("nested", 130, 64, 220, 40, 60);
    check("nested", 44, 26, 20, 40, 60);
    check("nested", 14, 111, 10, 210, 30);
    std::cout << "CLIP_OFFSET_REGRESSION backend=" << kBackendName
              << " status=" << (passed ? "PASS" : "FAIL")
              << " checks=" << checks << '\n';
    return passed;
}

} // namespace

int main()
{
    if (!glfwInit()) {
        return unavailable("glfw_init");
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#if defined(WHATSCANVAS_PARITY_OPENGLES)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(
        kCompositeParityWidth, kCompositeParityHeight,
        "WhatsCanvas filter parity", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return unavailable("context_creation");
    }
    glfwMakeContextCurrent(window);

    if (!wsc::Canvas::loadOpenGL(
            reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return unavailable("function_loading");
    }

#if !defined(WHATSCANVAS_PARITY_OPENGLES)
    const auto debugMessageCallback =
        reinterpret_cast<GLDebugMessageCallbackProc>(
            glfwGetProcAddress("glDebugMessageCallback"));
    if (debugMessageCallback != nullptr) {
        glEnable(kDebugOutput);
        glEnable(kDebugOutputSynchronous);
        debugMessageCallback(reportOpenGLDebugMessage, nullptr);
    }
#endif

    glDisable(GL_DITHER);
    glDisable(GL_MULTISAMPLE);
#if !defined(WHATSCANVAS_PARITY_OPENGLES)
    glDisable(GL_FRAMEBUFFER_SRGB);
#endif

    GLTarget target;
    if (!target.create()) {
        target.destroy();
        glfwDestroyWindow(window);
        glfwTerminate();
        std::cerr << "FILTER_PARITY backend=" << kBackendName
                  << " status=FAIL reason=incomplete_framebuffer\n";
        return 1;
    }

    auto canvas = wsc::Canvas::create(
        kBackend, kCompositeParityWidth, kCompositeParityHeight);
    bool rendered = canvas && canvas->initializeContext()
        && canvas->setOutputTarget(wsc::OutputTarget::GLFramebuffer(
            target.framebuffer, kCompositeParityWidth,
            kCompositeParityHeight, true));
    while (glGetError() != GL_NO_ERROR) {
    }
    rendered = rendered
        && whatscanvas::test::drawCompositeFilterParityScene(*canvas);
    const GLenum renderError = glGetError();

    std::vector<unsigned char> actual;
    wsc::Canvas::RenderStats stats;
    if (rendered) {
        glFinish();
        stats = canvas->getRenderStats();
        rendered = canvas->readPixelsRGBA(actual);
    }
    const GLenum readbackError = glGetError();

    std::vector<unsigned char> reference;
    wsc::Canvas::RenderStats referenceStats;
    const bool referenceRendered =
        whatscanvas::test::renderCompositeFilterParityScene(
            wsc::Canvas::Backend::Software, reference, &referenceStats);

    const bool statsPassed = rendered && referenceRendered
        && stats.filterCount == 3 && stats.filterPassCount == 7
        && stats.shaderProgramLinkCount > 0
        && stats.shaderStageCompileCount
            >= stats.shaderProgramLinkCount * 2u
        && stats.shaderCompileCpuTimeNs > 0
        && stats.shaderLinkCpuTimeNs > 0
        && referenceStats.filterCount == 3
        && referenceStats.filterPassCount == 9
        && referenceStats.shaderProgramLinkCount == 0
        && referenceStats.shaderStageCompileCount == 0;
    bool passed = rendered && referenceRendered && statsPassed;
    if (rendered && referenceRendered) {
        const auto diff = whatscanvas::test::comparePremultipliedRGBA(
            actual, reference, kCompositeParityWidth, kCompositeParityHeight);
        passed = whatscanvas::test::reportPixelParity(
            kBackendName, diff, whatscanvas::test::hashRGBA(actual),
            whatscanvas::test::hashRGBA(reference), kMaxChannelDifference, 0.75,
            kMaxBadPixelRatio,
            statsPassed, statsPassed ? nullptr : "unexpected_filter_stats");
        if (!passed) {
            std::cerr << "FILTER_PARITY_GL_ERROR"
                      << " backend=" << kBackendName
                      << " render=" << renderError
                      << " readback=" << readbackError << '\n';
            constexpr int samples[][2] = {
                {0, 0}, {16, 16}, {96, 64}, {191, 127}
            };
            for (const auto &sample : samples) {
                reportPixelSample(
                    "actual", actual, sample[0], sample[1]);
                reportPixelSample(
                    "reference", reference,
                    sample[0], sample[1]);
            }
        }
        if (!statsPassed) {
            std::cerr << "[FilterPixelParityTests] unexpected stats:"
                      << " actual_filters=" << stats.filterCount
                      << " actual_passes=" << stats.filterPassCount
                      << " reference_filters=" << referenceStats.filterCount
                      << " reference_passes=" << referenceStats.filterPassCount
                      << '\n';
        }
    } else {
        std::cerr << "FILTER_PARITY backend=" << kBackendName
                  << " status=FAIL reason=render_or_readback\n";
    }

    if (rendered) {
        passed = checkClipMaskOffsets(*canvas) && passed;
    }
    canvas.reset();
    target.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
    return passed ? 0 : 1;
}
