#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "wsc/wsc.h"
#include "opengl/GLTextureUtils.h"

namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 64;

#if defined(WHATSCANVAS_TEST_OPENGLES)
constexpr wsc::Canvas::Backend kBackend =
    wsc::Canvas::Backend::OpenGLES;
#else
constexpr wsc::Canvas::Backend kBackend =
    wsc::Canvas::Backend::OpenGL;
#endif

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr
            << "[OpenGLMultiTextureBatchTests] FAIL: "
            << message << '\n';
    }
    return condition;
}

} // namespace

int main()
{
    if (!glfwInit()) {
        std::cout
            << "[OpenGLMultiTextureBatchTests] SKIP: glfwInit failed.\n";
        return 0;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#if defined(WHATSCANVAS_TEST_OPENGLES)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif
    GLFWwindow *window = glfwCreateWindow(
        kWidth, kHeight, "OpenGLMultiTextureBatchTests",
        nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        std::cout
            << "[OpenGLMultiTextureBatchTests] SKIP: "
               "requested GL context unavailable.\n";
        return 0;
    }
    glfwMakeContextCurrent(window);
    if (!wsc::Canvas::loadOpenGL(
            reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(
                glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return expect(false, "unable to load OpenGL") ? 0 : 1;
    }

    auto canvas = wsc::Canvas::create(kBackend, kWidth, kHeight);
    if (!canvas || !canvas->initializeContext()
        || !canvas->setOutputTarget(
            wsc::OutputTarget::GLFramebuffer(
                0, kWidth, kHeight, true))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return expect(false, "unable to initialize canvas") ? 0 : 1;
    }

    constexpr std::array<std::array<std::uint8_t, 4>, 4> colors = {{
        {{255, 0, 0, 255}},
        {{0, 255, 0, 255}},
        {{0, 0, 255, 255}},
        {{255, 255, 0, 255}}
    }};
    std::array<wsc::Image, colors.size()> images;
    bool ok = true;
    for (std::size_t index = 0; index < images.size(); ++index) {
        const std::vector<unsigned char> texel(
            colors[index].begin(), colors[index].end());
        ok = expect(
                 images[index].loadFromRGBA(
                     *canvas, texel, 1, 1, false),
                 "texture upload should succeed")
            && ok;
    }

    wsc::Paint paint;
    paint.setColor(wsc::Color::WHITE);
    canvas->beginFrame();
    canvas->drawColor(wsc::Color::BLACK);
    for (int sprite = 0; sprite < 32; ++sprite) {
        const int column = sprite % 16;
        const int row = sprite / 16;
        canvas->drawImage(
            images[static_cast<std::size_t>(sprite) % images.size()],
            wsc::RectF(
                static_cast<float>(column * 8),
                static_cast<float>(row * 32),
                8.0f, 32.0f),
            paint);
    }
    canvas->endFrame();
    glFinish();

    const wsc::Canvas::RenderStats stats =
        canvas->getRenderStats();
    ok = expect(
             stats.drawCallCount == 2,
             "clear plus 32 ordered sprites should use two draws")
        && ok;
    ok = expect(
             stats.mergedBatchCount >= 1,
             "multi-texture sprites should report a merged batch")
        && ok;

    std::vector<unsigned char> pixels;
    ok = expect(
             canvas->readPixelsRGBA(pixels),
             "framebuffer readback should succeed")
        && ok;
    if (pixels.size()
        == static_cast<std::size_t>(kWidth * kHeight * 4)) {
        for (int sprite = 0; sprite < 16; ++sprite) {
            const int x = sprite * 8 + 4;
            const int y = 16;
            const std::size_t offset =
                (static_cast<std::size_t>(y) * kWidth + x) * 4u;
            const auto &expected =
                colors[static_cast<std::size_t>(sprite) % colors.size()];
            ok = expect(
                     pixels[offset] == expected[0]
                         && pixels[offset + 1u] == expected[1]
                         && pixels[offset + 2u] == expected[2]
                         && pixels[offset + 3u] == expected[3],
                     "each sprite must sample its assigned texture slot")
                && ok;
        }
    } else {
        ok = expect(false, "readback should contain a complete frame") && ok;
    }

    // Glyph atlases are R8 textures. Their swizzle must expose white RGB and
    // move the red coverage channel into alpha on both desktop GL and GLES.
    const auto alphaTexture = wsc::opengl::createTextureAlpha8(
        1, 1, std::vector<unsigned char>{128});
    ok = expect(alphaTexture.isValid(), "alpha texture upload should succeed")
        && ok;
    if (alphaTexture.isValid()) {
        GLint previousTexture = 0;
        GLint swizzleR = 0;
        GLint swizzleG = 0;
        GLint swizzleB = 0;
        GLint swizzleA = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
        glBindTexture(GL_TEXTURE_2D, alphaTexture.value);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, &swizzleR);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, &swizzleG);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, &swizzleB);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, &swizzleA);
        ok = expect(swizzleR == GL_ONE && swizzleG == GL_ONE
                        && swizzleB == GL_ONE && swizzleA == GL_RED,
                    "R8 glyph atlas must sample as white alpha coverage")
            && ok;
        ok = expect(glGetError() == GL_NO_ERROR,
                    "alpha swizzle must be valid for the active GL API")
            && ok;
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
        wsc::opengl::destroyTexture(alphaTexture);
    }

    canvas.reset();
    glfwDestroyWindow(window);
    glfwTerminate();
    return ok ? 0 : 1;
}
