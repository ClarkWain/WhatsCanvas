// Pixel-level regression for the OpenGL LCD text compositing path.  DirectWrite
// produces independent R/G/B coverage; ordinary SrcOver consumes only alpha and
// turns this sample gray.  The dual-source path must retain three values on an
// opaque white target: black text with {1/4, 1/2, 3/4} coverage becomes roughly
// {191, 128, 64}.

#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include "wsc/Surface.h"
#endif

#include "command/DrawCommand.h"
#include "render/Renderer.h"
#include "text/DirectWriteTextBackend.h"
#include "wsc/Canvas.h"

namespace {

constexpr int kLogicalWidth = 192;
constexpr int kLogicalHeight = 96;
constexpr int kFramebufferWidth = 288;
constexpr int kFramebufferHeight = 144;

bool approximatelyEqual(unsigned char value, int expected, int tolerance = 8)
{
    return std::abs(static_cast<int>(value) - expected) <= tolerance;
}

bool hasRgbSubpixelCoverage(const wsc::text::TextRenderResult &result)
{
    if (result.kind != wsc::text::TextRenderKind::Bitmap
        || !result.bitmapIsClearType
        || result.bitmapPixels.size()
            < static_cast<std::size_t>(result.bitmapWidth * result.bitmapHeight * 4)) {
        return false;
    }
    for (std::size_t i = 0; i + 3 < result.bitmapPixels.size(); i += 4) {
        const unsigned char r = result.bitmapPixels[i + 0];
        const unsigned char g = result.bitmapPixels[i + 1];
        const unsigned char b = result.bitmapPixels[i + 2];
        if ((r != g || g != b) && (r != 0 || g != 0 || b != 0)) {
            return true;
        }
    }
    return false;
}

bool framebufferHasRgbDistinctTextPixel(const std::vector<unsigned char> &pixels)
{
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const unsigned char r = pixels[i + 0];
        const unsigned char g = pixels[i + 1];
        const unsigned char b = pixels[i + 2];
        // Ignore the white background and require a visibly covered pixel
        // whose three LCD channels survived final framebuffer composition.
        if ((r < 250 || g < 250 || b < 250) && (r != g || g != b)) {
            return true;
        }
    }
    return false;
}

struct FramebufferStats {
    std::size_t nonWhite = 0;
    std::size_t rgbDistinct = 0;
};

FramebufferStats framebufferStats(const std::vector<unsigned char> &pixels)
{
    FramebufferStats stats;
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const unsigned char r = pixels[i + 0];
        const unsigned char g = pixels[i + 1];
        const unsigned char b = pixels[i + 2];
        const bool nonWhite = r < 250 || g < 250 || b < 250;
        stats.nonWhite += nonWhite ? 1u : 0u;
        stats.rgbDistinct += nonWhite && (r != g || g != b) ? 1u : 0u;
    }
    return stats;
}

void printFramebufferStats(const char *label, const std::vector<unsigned char> &pixels)
{
    const auto stats = framebufferStats(pixels);
    std::cout << "[ClearTypeCompositingTests] " << label << ": " << stats.nonWhite
              << " non-white, " << stats.rgbDistinct << " RGB-distinct pixels." << std::endl;
}

} // namespace

int main()
{
    if (!glfwInit()) {
        std::cout << "[ClearTypeCompositingTests] SKIP: glfwInit failed." << std::endl;
        return 0;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow *window = glfwCreateWindow(kFramebufferWidth, kFramebufferHeight,
                                          "ClearTypeCompositingTests", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        std::cout << "[ClearTypeCompositingTests] SKIP: GLFW 3.3 context unavailable." << std::endl;
        return 0;
    }
    glfwMakeContextCurrent(window);
    if (!wsc::Canvas::loadOpenGL(reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: unable to load OpenGL." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    GLint maxDualSource = 0;
    glGetIntegerv(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS, &maxDualSource);
    if (maxDualSource < 1) {
        std::cout << "[ClearTypeCompositingTests] SKIP: dual-source blending unavailable." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }

#if defined(_WIN32)
    // This is deliberately before the GL blend test.  It protects the real
    // DirectWrite -> WIC/D2D raster stage: an empty bitmap used to make every
    // Todo label disappear even though the synthetic dual-source mask below
    // still passed.  Render two physical sizes matching 100% and 150% UI.
    wsc::text::DirectWriteBackendOptions nativeOptions;
    nativeOptions.rasterMode = wsc::text::DirectWriteRasterMode::ClearType;
    auto directWrite = wsc::text::createDirectWriteTextBackend(nativeOptions);
    if (!directWrite) {
        std::cout << "[ClearTypeCompositingTests] SKIP: DirectWrite unavailable." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    }
    wsc::Paint nativePaint;
    nativePaint.setColor(wsc::Color(0, 0, 0, 255));
    nativePaint.setFontFamily("Segoe UI");
    nativePaint.setTextBaseline(wsc::Paint::TextBaseline::BOTTOM);
    nativePaint.setTextSize(18.0f);
    const auto oneX = directWrite->renderText("My day", 12.0f, 48.0f, nativePaint);
    nativePaint.setTextSize(27.0f);
    const auto onePointFiveX = directWrite->renderText("My day", 18.0f, 72.0f, nativePaint);
    if (!hasRgbSubpixelCoverage(oneX) || !hasRgbSubpixelCoverage(onePointFiveX)) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: DirectWrite ClearType raster was empty or lost RGB coverage."
                  << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
#endif

    constexpr int kSize = 8;
    Renderer renderer;
    renderer.initializeBackend();
    renderer.setViewport(kSize, kSize);
    glViewport(0, 0, kSize, kSize);
    glClearColor(1.f, 1.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    // A one-pixel black ClearType mask.  Alpha deliberately equals max(RGB),
    // which is what DirectWrite emits; a conventional SrcOver result would be
    // neutral 64/64/64 and fails the assertions below.
    const std::vector<unsigned char> mask = {64, 128, 192, 192};
    const auto texture = renderer.createImageResourceRGBA(1, 1, mask);
    if (!texture || !texture->isValid()) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: texture creation failed." << std::endl;
        renderer.finalizeBackend();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    DrawImageData draw;
    draw.imageResource = texture;
    draw.width = static_cast<float>(kSize);
    draw.height = static_cast<float>(kSize);
    draw.tintColor[0] = 0.f;
    draw.tintColor[1] = 0.f;
    draw.tintColor[2] = 0.f;
    draw.tintColor[3] = 1.f;
    draw.sampling = DrawImageSampling::Nearest;
    // Baseline: an ordinary RGBA image still uses the unchanged alpha/SrcOver
    // path, so max(alpha)=192 produces neutral dark gray over white.
    draw.clearTypeMask = false;
    renderer.submit(std::make_unique<DrawImageCommand>(draw));
    renderer.flush();

    std::vector<unsigned char> grayscalePixels;
    const bool grayscaleRead = renderer.readPixelsRGBA(grayscalePixels);
    const bool grayscaleOk = grayscaleRead
        && grayscalePixels.size() == static_cast<std::size_t>(kSize * kSize * 4)
        && approximatelyEqual(grayscalePixels[0], 63) && approximatelyEqual(grayscalePixels[1], 63)
        && approximatelyEqual(grayscalePixels[2], 63);
    renderer.clear();
    glClearColor(1.f, 1.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    // An LCD source that cannot use dual-source blending must become a
    // grayscale alpha mask. With a green tint the expected SrcOver result is
    // (63,255,63); treating coverage.rgb as image color would darken green.
    draw.rgbCoverageMask = true;
    draw.tintColor[0] = 0.f;
    draw.tintColor[1] = 1.f;
    draw.tintColor[2] = 0.f;
    renderer.submit(std::make_unique<DrawImageCommand>(draw));
    renderer.flush();
    std::vector<unsigned char> fallbackPixels;
    const bool fallbackRead = renderer.readPixelsRGBA(fallbackPixels);
    const bool fallbackOk = fallbackRead
        && fallbackPixels.size() == static_cast<std::size_t>(kSize * kSize * 4)
        && approximatelyEqual(fallbackPixels[0], 63)
        && approximatelyEqual(fallbackPixels[1], 255)
        && approximatelyEqual(fallbackPixels[2], 63);
    renderer.clear();
    glClearColor(1.f, 1.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    draw.tintColor[0] = 0.f;
    draw.tintColor[1] = 0.f;
    draw.tintColor[2] = 0.f;
    draw.clearTypeMask = true;
    renderer.submit(std::make_unique<DrawImageCommand>(draw));
    renderer.flush();

    std::vector<unsigned char> pixels;
    const bool read = renderer.readPixelsRGBA(pixels);
    if (!grayscaleOk) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: ordinary RGBA/SrcOver changed; got ("
                  << (grayscalePixels.empty() ? -1 : static_cast<int>(grayscalePixels[0])) << ','
                  << (grayscalePixels.size() < 2 ? -1 : static_cast<int>(grayscalePixels[1])) << ','
                  << (grayscalePixels.size() < 3 ? -1 : static_cast<int>(grayscalePixels[2])) << ")." << std::endl;
        return 1;
    }
    if (!fallbackOk) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: LCD grayscale fallback changed colored text; got ("
                  << (fallbackPixels.empty() ? -1 : static_cast<int>(fallbackPixels[0])) << ','
                  << (fallbackPixels.size() < 2 ? -1 : static_cast<int>(fallbackPixels[1])) << ','
                  << (fallbackPixels.size() < 3 ? -1 : static_cast<int>(fallbackPixels[2])) << ")." << std::endl;
        return 1;
    }
    if (!read || pixels.size() != static_cast<std::size_t>(kSize * kSize * 4)) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: framebuffer readback failed." << std::endl;
        return 1;
    }

    const unsigned char r = pixels[0];
    const unsigned char g = pixels[1];
    const unsigned char b = pixels[2];
    if (!(approximatelyEqual(r, 191) && approximatelyEqual(g, 127) && approximatelyEqual(b, 63)
          && r > g && g > b)) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: expected RGB-distinct ClearType result near "
                  << "(191,127,63), got (" << static_cast<int>(r) << ','
                  << static_cast<int>(g) << ',' << static_cast<int>(b) << ")." << std::endl;
        return 1;
    }

    // Exercise Canvas setup as well.  The DirectWrite raster assertions above
    // are the authoritative native-text check; Canvas readback shares the
    // current default GL framebuffer with the raw blend probe, so it cannot by
    // itself prove a text run was emitted.
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL,
                                      kFramebufferWidth, kFramebufferHeight);
    if (!canvas || !canvas->initializeContext()) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: unable to initialize Canvas text path." << std::endl;
        return 1;
    }
    canvas->setSize(kFramebufferWidth, kFramebufferHeight);
    // First render at 1x: this is the path where ClearType is eligible, so it
    // must be visible through the full Canvas/native-present pipeline.
    canvas->setDevicePixelRatio(1.0f);
    // Bind the real window framebuffer directly. Window presentation uses an
    // internal swapchain target, so Canvas::readPixelsRGBA() before present
    // does not read that frame, while reading afterward observes the newly
    // swapped back buffer. Neither is a valid compositing assertion.
    if (!canvas->setOutputTarget(wsc::OutputTarget::GLFramebuffer(
            0, kFramebufferWidth, kFramebufferHeight, true))) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: unable to configure the GL framebuffer." << std::endl;
        return 1;
    }
    if (!canvas->setTextBackend(wsc::Canvas::TextBackend::DirectWrite,
                                wsc::Canvas::TextRenderMode::ClearType)) {
        std::cout << "[ClearTypeCompositingTests] SKIP: DirectWrite unavailable." << std::endl;
        return 0;
    }
    wsc::Paint textPaint;
    textPaint.setColor(wsc::Color(0, 0, 0, 255));
    textPaint.setTextSize(18.0f);
    textPaint.setFontFamily("Segoe UI");
    textPaint.setTextBaseline(wsc::Paint::TextBaseline::BOTTOM);
    canvas->beginFrame();
    canvas->drawColor(wsc::Color(255, 255, 255, 255));
    // Todo's document is a ScrollView, so text is normally emitted inside a
    // saved, clipped subtree rather than at the root canvas state.
    canvas->save();
    canvas->clipRect(wsc::RectF(0.0f, 0.0f, static_cast<float>(kLogicalWidth),
                                static_cast<float>(kLogicalHeight)));
    canvas->drawText("My day", 12.0f, 48.0f, textPaint);
    canvas->restore();
    canvas->endFrame();
    std::vector<unsigned char> textPixels;
    if (!canvas->readPixelsRGBA(textPixels)) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: 1x Canvas text readback failed." << std::endl;
        return 1;
    }
    printFramebufferStats("1x before present", textPixels);

    // Re-run the same framework-equivalent text path at 150%. The glyph is
    // rasterized at physical resolution and normalized by the same 1.5 root
    // transform, so RGB ClearType coverage must survive final composition.
    canvas->setDevicePixelRatio(1.5f);
    canvas->beginFrame();
    canvas->drawColor(wsc::Color(255, 255, 255, 255));
    canvas->drawText("My day", 12.0f, 48.0f, textPaint);
    canvas->endFrame();
    std::vector<unsigned char> scaledTextPixels;
    if (!canvas->readPixelsRGBA(scaledTextPixels)) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: 150% Canvas text readback failed." << std::endl;
        return 1;
    }
    printFramebufferStats("1.5x before present", scaledTextPixels);
    if (!framebufferHasRgbDistinctTextPixel(textPixels)) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: 1x Canvas text lost RGB ClearType coverage."
                  << std::endl;
        return 1;
    }
    if (!framebufferHasRgbDistinctTextPixel(scaledTextPixels)) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: 1.5x Canvas text lost RGB ClearType coverage."
                  << std::endl;
        return 1;
    }

    // Host-owned framebuffers are conservatively non-opaque unless the host
    // opts in. The same LCD bitmap must therefore take the grayscale fallback
    // rather than dual-source composition.
    if (!canvas->setOutputTarget(wsc::OutputTarget::GLFramebuffer(
            0, kFramebufferWidth, kFramebufferHeight))) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: unable to configure unknown-alpha framebuffer."
                  << std::endl;
        return 1;
    }
    canvas->setDevicePixelRatio(1.0f);
    canvas->beginFrame();
    canvas->drawColor(wsc::Color(255, 255, 255, 255));
    canvas->drawText("My day", 12.0f, 48.0f, textPaint);
    canvas->endFrame();
    std::vector<unsigned char> fallbackTextPixels;
    if (!canvas->readPixelsRGBA(fallbackTextPixels)
        || framebufferHasRgbDistinctTextPixel(fallbackTextPixels)) {
        std::cerr << "[ClearTypeCompositingTests] FAIL: unknown-alpha target did not use grayscale fallback."
                  << std::endl;
        return 1;
    }
    canvas->shutdown();
    renderer.finalizeBackend();
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "[ClearTypeCompositingTests] PASS: DirectWrite raster retained RGB coverage; composed sample is ("
              << static_cast<int>(r) << ',' << static_cast<int>(g) << ','
              << static_cast<int>(b) << ")."
              << std::endl;
    return 0;
}
