#include <iostream>
#include <string>
#include <vector>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "wsc/wsc.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace wsc;

namespace {

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;

Paint solid(const Color &color)
{
    Paint paint;
    paint.setStyle(Paint::Style::FILL);
    paint.setColor(color);
    paint.setAntiAlias(true);
    return paint;
}

Paint textStyle(float size, const Color &color)
{
    Paint paint = solid(color);
    paint.setTextSize(size);
    paint.setTextBaseline(Paint::TextBaseline::TOP);
    return paint;
}

void strokeRoundRect(Canvas &canvas, const RectF &bounds, float radius,
                     const Color &color, float width)
{
    Path path;
    path.addRoundRect(bounds, radius);
    Paint border;
    border.setStyle(Paint::Style::STROKE);
    border.setStrokeColor(color);
    border.setStrokeWidth(width);
    border.setAntiAlias(true);
    canvas.drawPath(path, border);
}

void drawPanelContent(Canvas &canvas, const RectF &bounds,
                      const std::string &title, const Color &accent)
{
    const float x = bounds.getX();
    const float y = bounds.getY();
    const float width = bounds.getWidth();

    canvas.drawCircle(x + 54.0f, y + 58.0f, 10.0f, solid(accent));
    canvas.drawText(title, x + 82.0f, y + 39.0f,
                    textStyle(22.0f, Color(255, 255, 255, 238)));
    canvas.drawText("BACKDROP BLUR", x + 82.0f, y + 72.0f,
                    textStyle(13.0f, Color(242, 246, 252, 145)));

    canvas.drawRoundRect(RectF(x + 40.0f, y + 120.0f, width - 80.0f, 1.5f),
                         0.75f, solid(Color(255, 255, 255, 55)));

    Paint ring;
    ring.setStyle(Paint::Style::STROKE);
    ring.setStrokeColor(Color(255, 255, 255, 185));
    ring.setStrokeWidth(7.0f);
    ring.setAntiAlias(true);
    canvas.drawCircle(x + width * 0.5f, y + 272.0f, 78.0f, ring);
    canvas.drawCircle(x + width * 0.5f, y + 272.0f, 30.0f,
                      solid(Color(accent.r(), accent.g(), accent.b(), 210.0f / 255.0f)));

    const float barWidths[] = {0.72f, 0.48f, 0.62f};
    for (int i = 0; i < 3; ++i) {
        const float barY = y + 410.0f + static_cast<float>(i) * 42.0f;
        canvas.drawRoundRect(RectF(x + 48.0f, barY, width - 96.0f, 7.0f),
                             3.5f, solid(Color(255, 255, 255, 38)));
        canvas.drawRoundRect(RectF(x + 48.0f, barY,
                                   (width - 96.0f) * barWidths[i], 7.0f),
                             3.5f, solid(Color(accent.r(), accent.g(), accent.b(),
                                               205.0f / 255.0f)));
    }
}

void drawGlassPanel(Canvas &canvas, const RectF &bounds, float blurRadius,
                    const std::string &title, const Color &accent)
{
    constexpr float cornerRadius = 42.0f;
    canvas.drawRoundRect(RectF(bounds.getX(), bounds.getY() + 20.0f,
                               bounds.getWidth(), bounds.getHeight()),
                         cornerRadius, solid(Color(5, 8, 18, 72)));

    LayerOptions options;
    options.setBackdropFilter(ImageFilter::blur(blurRadius));
    canvas.saveLayer(bounds, solid(Color(255, 255, 255, 255)), options);

    Path clip;
    clip.addRoundRect(bounds, cornerRadius);
    canvas.clipPath(clip);

    Paint tint;
    tint.setLinearGradient(
        bounds.getX(), bounds.getY(),
        bounds.getX(), bounds.getY() + bounds.getHeight(),
        {
            Paint::ColorStop(0.0f, Color(255, 255, 255, 76)),
            Paint::ColorStop(0.28f, Color(37, 45, 66, 96)),
            Paint::ColorStop(1.0f, Color(13, 18, 32, 142)),
        });
    canvas.drawRect(bounds, tint);

    Paint highlight;
    highlight.setLinearGradient(bounds.getX(), bounds.getY(),
                                bounds.getX(), bounds.getY() + 150.0f,
                                Color(255, 255, 255, 42),
                                Color(255, 255, 255, 0));
    canvas.drawRect(RectF(bounds.getX(), bounds.getY(),
                          bounds.getWidth(), 150.0f), highlight);
    strokeRoundRect(canvas, bounds, cornerRadius,
                    Color(255, 255, 255, 128), 1.5f);

    drawPanelContent(canvas, bounds, title, accent);
    canvas.restore();
}

} // namespace

int main(int argc, char **argv)
{
    const std::string outputPath =
        argc > 1 ? argv[1] : "images/image-filter-showcase.png";
    const std::string backgroundPath =
        argc > 2 ? argv[2] : "images/image-filter-background.png";

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(kWidth, kHeight, "Image Filter Showcase",
                                          nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create OpenGL window\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!Canvas::loadOpenGL(
            reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        std::cerr << "Failed to load OpenGL\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    auto canvas = Canvas::create(Canvas::Backend::OpenGL, kWidth, kHeight);
    if (!canvas || !canvas->initializeContext()) {
        std::cerr << "Failed to initialize WhatsCanvas\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    canvas->setSize(kWidth, kHeight);
    canvas->setOutputTarget(OutputTarget::GLFramebuffer(0, kWidth, kHeight, true));

    Image background;
    if (!canvas->loadImage(background, backgroundPath.c_str())) {
        std::cerr << "Failed to load " << backgroundPath << '\n';
        canvas.reset();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    canvas->beginFrame();
    Paint imagePaint = solid(Color(255, 255, 255, 255));
    imagePaint.setImageSampling(Paint::ImageSampling::LINEAR);
    canvas->drawImage(background,
                      RectF(0.0f, 0.0f, static_cast<float>(kWidth),
                            static_cast<float>(kHeight)),
                      imagePaint);

    canvas->drawText("WHATSCANVAS", 82.0f, 58.0f,
                     textStyle(27.0f, Color(255, 255, 255, 238)));
    canvas->drawText("IMAGE FILTER / BACKDROP BLUR", 82.0f, 94.0f,
                     textStyle(14.0f, Color(255, 255, 255, 158)));

    const RectF panels[] = {
        RectF(150.0f, 238.0f, 480.0f, 610.0f),
        RectF(720.0f, 238.0f, 480.0f, 610.0f),
        RectF(1290.0f, 238.0f, 480.0f, 610.0f),
    };
    drawGlassPanel(*canvas, panels[0], 14.0f, "SOFT",
                   Color(117, 238, 227, 255));
    drawGlassPanel(*canvas, panels[1], 30.0f, "BALANCED",
                   Color(255, 166, 116, 255));
    drawGlassPanel(*canvas, panels[2], 52.0f, "DEEP",
                   Color(211, 238, 91, 255));

    canvas->drawText("Real OpenGL framebuffer capture", 82.0f, 1016.0f,
                     textStyle(15.0f, Color(255, 255, 255, 145)));
    canvas->endFrame();

    GLint framebuffer = 0;
    GLint viewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    const Canvas::RenderStats stats = canvas->getRenderStats();
    std::cout << "Framebuffer binding " << framebuffer << ", viewport "
              << viewport[0] << ',' << viewport[1] << ' '
              << viewport[2] << 'x' << viewport[3]
              << ", commands " << stats.commandCount
              << ", draws " << stats.drawCallCount
              << ", GL error " << glGetError() << '\n';

    std::vector<unsigned char> pixels;
    const bool read = canvas->readPixelsRGBA(pixels);
    const bool wrote = read
        && pixels.size() == static_cast<std::size_t>(kWidth) * kHeight * 4u
        && stbi_write_png(outputPath.c_str(), kWidth, kHeight, 4,
                          pixels.data(), kWidth * 4) != 0;

    canvas.reset();
    glfwDestroyWindow(window);
    glfwTerminate();

    if (!wrote) {
        std::cerr << "Failed to write " << outputPath << '\n';
        return 1;
    }
    std::cout << "Wrote " << outputPath << " (" << kWidth << 'x' << kHeight << ")\n";
    return 0;
}
