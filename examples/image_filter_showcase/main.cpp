#include <algorithm>
#include <cstdlib>
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

void drawBackground(Canvas &canvas)
{
    Paint background;
    background.setLinearGradient(
        0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight),
        {
            Paint::ColorStop(0.0f, Color(20, 31, 52, 255)),
            Paint::ColorStop(0.42f, Color(30, 74, 96, 255)),
            Paint::ColorStop(0.72f, Color(83, 50, 107, 255)),
            Paint::ColorStop(1.0f, Color(18, 24, 38, 255)),
        });
    canvas.drawRect(RectF(0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight)), background);

    Paint cyan;
    cyan.setLinearGradient(80.0f, 160.0f, 680.0f, 880.0f,
                           Color(22, 213, 205, 235), Color(24, 111, 180, 150));
    canvas.drawRoundRect(RectF(70.0f, 190.0f, 620.0f, 180.0f), 90.0f, cyan);
    canvas.drawRoundRect(RectF(210.0f, 720.0f, 560.0f, 150.0f), 75.0f, cyan);

    Paint coral;
    coral.setLinearGradient(1160.0f, 120.0f, 1800.0f, 940.0f,
                            Color(255, 115, 105, 240), Color(214, 59, 139, 165));
    canvas.drawRoundRect(RectF(1260.0f, 130.0f, 520.0f, 170.0f), 85.0f, coral);
    canvas.drawRoundRect(RectF(1140.0f, 750.0f, 650.0f, 170.0f), 85.0f, coral);

    Paint lime = solid(Color(203, 236, 112, 225));
    canvas.drawCircle(950.0f, 190.0f, 105.0f, lime);
    Paint violet = solid(Color(137, 102, 238, 225));
    canvas.drawCircle(1020.0f, 870.0f, 135.0f, violet);

    Paint stripe = solid(Color(255, 255, 255, 58));
    for (int i = -3; i < 17; ++i) {
        canvas.save();
        canvas.translate(static_cast<float>(i * 150), 0.0f);
        canvas.rotate(-0.35f);
        canvas.drawRect(RectF(0.0f, -280.0f, 28.0f, 1580.0f), stripe);
        canvas.restore();
    }

    Paint dot = solid(Color(255, 255, 255, 160));
    for (int y = 150; y < kHeight; y += 110) {
        for (int x = 100; x < kWidth; x += 120) {
            const float radius = ((x / 120 + y / 110) % 3 == 0) ? 8.0f : 4.0f;
            canvas.drawCircle(static_cast<float>(x), static_cast<float>(y), radius, dot);
        }
    }
}

void drawGlassPanel(Canvas &canvas, const RectF &bounds, float cornerRadius, float blurRadius,
                    const Color &tintColor, const std::string &title, const std::string &detail)
{
    Paint panelShadow = solid(Color(3, 8, 20, 82));
    canvas.drawRoundRect(RectF(bounds.getX(), bounds.getY() + 18.0f,
                               bounds.getWidth(), bounds.getHeight()),
                         cornerRadius, panelShadow);

    LayerOptions options;
    options.setBackdropFilter(ImageFilter::blur(blurRadius));

    Paint composite;
    composite.setColor(Color(255, 255, 255, 255));
    canvas.saveLayer(bounds, composite, options);

    Path clip;
    clip.addRoundRect(bounds, cornerRadius);
    canvas.clipPath(clip);

    Paint tint;
    tint.setLinearGradient(bounds.getX(), bounds.getY(),
                           bounds.getX() + bounds.getWidth(), bounds.getY() + bounds.getHeight(),
                           tintColor, Color(255, 255, 255, 24));
    canvas.drawRect(bounds, tint);

    Paint highlight = solid(Color(255, 255, 255, 34));
    canvas.drawRoundRect(RectF(bounds.getX() + 2.0f, bounds.getY() + 2.0f,
                               bounds.getWidth() - 4.0f, bounds.getHeight() * 0.32f),
                         cornerRadius - 2.0f, highlight);

    Paint border;
    border.setStyle(Paint::Style::STROKE);
    border.setStrokeColor(Color(255, 255, 255, 125));
    border.setStrokeWidth(2.0f);
    border.setAntiAlias(true);
    canvas.drawPath(clip, border);

    Paint titlePaint = solid(Color(255, 255, 255, 245));
    titlePaint.setTextSize(bounds.getWidth() > 600.0f ? 42.0f : 30.0f);
    titlePaint.setTextBaseline(Paint::TextBaseline::TOP);
    canvas.drawText(title, bounds.getX() + 38.0f, bounds.getY() + 34.0f, titlePaint);

    Paint detailPaint = solid(Color(236, 244, 255, 195));
    detailPaint.setTextSize(bounds.getWidth() > 600.0f ? 24.0f : 20.0f);
    detailPaint.setTextBaseline(Paint::TextBaseline::TOP);
    canvas.drawText(detail, bounds.getX() + 40.0f, bounds.getY() + 94.0f, detailPaint);

    Paint divider = solid(Color(255, 255, 255, 62));
    canvas.drawRoundRect(RectF(bounds.getX() + 40.0f, bounds.getY() + 142.0f,
                               bounds.getWidth() - 80.0f, 2.0f), 1.0f, divider);

    Paint metric = solid(Color(255, 255, 255, 224));
    metric.setTextSize(bounds.getWidth() > 600.0f ? 64.0f : 44.0f);
    metric.setTextBaseline(Paint::TextBaseline::TOP);
    canvas.drawText(std::to_string(static_cast<int>(blurRadius)) + " px",
                    bounds.getX() + 40.0f, bounds.getY() + 176.0f, metric);

    Paint caption = solid(Color(228, 238, 252, 165));
    caption.setTextSize(19.0f);
    caption.setTextBaseline(Paint::TextBaseline::TOP);
    canvas.drawText("REAL BACKDROP SAMPLE", bounds.getX() + 42.0f,
                    bounds.getY() + bounds.getHeight() - 54.0f, caption);

    canvas.restore();
}

} // namespace

int main(int argc, char **argv)
{
    const std::string outputPath = argc > 1 ? argv[1] : "image-filter-showcase.png";
    const int panelCount = argc > 2 ? std::clamp(std::atoi(argv[2]), 0, 3) : 3;

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

    GLFWwindow *window = glfwCreateWindow(kWidth, kHeight, "Image Filter Showcase", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create OpenGL window\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!Canvas::loadOpenGL(reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
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

    canvas->beginFrame();
    drawBackground(*canvas);

    Paint heading = solid(Color(255, 255, 255, 250));
    heading.setTextSize(66.0f);
    heading.setTextBaseline(Paint::TextBaseline::TOP);
    canvas->drawText("WHATSCANVAS / IMAGE FILTERS", 90.0f, 62.0f, heading);

    Paint subheading = solid(Color(220, 233, 248, 190));
    subheading.setTextSize(25.0f);
    subheading.setTextBaseline(Paint::TextBaseline::TOP);
    canvas->drawText("The panels below blur the actual rendered scene behind them.", 94.0f, 140.0f, subheading);

    if (panelCount >= 1) {
        drawGlassPanel(*canvas, RectF(90.0f, 300.0f, 450.0f, 440.0f), 38.0f, 10.0f,
                       Color(190, 235, 255, 54), "SOFT GLASS", "Light diffusion");
    }
    if (panelCount >= 2) {
        drawGlassPanel(*canvas, RectF(575.0f, 245.0f, 770.0f, 545.0f), 48.0f, 28.0f,
                       Color(255, 255, 255, 58), "FROSTED GLASS", "Balanced realtime backdrop blur");
    }
    if (panelCount >= 3) {
        drawGlassPanel(*canvas, RectF(1380.0f, 300.0f, 450.0f, 440.0f), 38.0f, 52.0f,
                       Color(244, 218, 255, 58), "DEEP FROST", "Strong diffusion");
    }

    Paint footer = solid(Color(226, 236, 250, 150));
    footer.setTextSize(21.0f);
    footer.setTextBaseline(Paint::TextBaseline::TOP);
    canvas->drawText("OpenGL GPU capture  |  1920 x 1080  |  no SVG, no mockup", 90.0f, 995.0f, footer);

    canvas->endFrame();

    const Canvas::RenderStats stats = canvas->getRenderStats();
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    GLint framebuffer = 0;
    GLint viewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    std::cout << "Framebuffer " << framebufferWidth << 'x' << framebufferHeight
              << ", binding " << framebuffer << ", viewport "
              << viewport[0] << ',' << viewport[1] << ' '
              << viewport[2] << 'x' << viewport[3]
              << ", commands " << stats.commandCount
              << ", draws " << stats.drawCallCount
              << ", GL error " << glGetError() << '\n';

    std::vector<unsigned char> pixels;
    const bool read = canvas->readPixelsRGBA(pixels);
    const bool wrote = read
        && pixels.size() == static_cast<std::size_t>(kWidth) * kHeight * 4u
        && stbi_write_png(outputPath.c_str(), kWidth, kHeight, 4, pixels.data(), kWidth * 4) != 0;

    canvas.reset();
    glfwDestroyWindow(window);
    glfwTerminate();

    if (!wrote) {
        std::cerr << "Failed to write " << outputPath << '\n';
        return 1;
    }
    std::cout << "Wrote " << outputPath << " (" << kWidth << "x" << kHeight << ")\n";
    return 0;
}
