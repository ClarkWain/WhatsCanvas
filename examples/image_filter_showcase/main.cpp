#include <cmath>
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

void drawAlbumCover(Canvas &canvas, const RectF &bounds,
                    const Color &start, const Color &end, int motif)
{
    canvas.save();
    Path clip;
    clip.addRoundRect(bounds, 24.0f);
    canvas.clipPath(clip);

    Paint background;
    background.setLinearGradient(bounds.getX(), bounds.getY(),
                                 bounds.getX() + bounds.getWidth(),
                                 bounds.getY() + bounds.getHeight(),
                                 start, end);
    canvas.drawRect(bounds, background);

    const float cx = bounds.getX() + bounds.getWidth() * 0.5f;
    const float cy = bounds.getY() + bounds.getHeight() * 0.5f;
    if (motif == 0) {
        Paint ring;
        ring.setStyle(Paint::Style::STROKE);
        ring.setStrokeColor(Color(255, 255, 255, 158));
        ring.setStrokeWidth(18.0f);
        ring.setAntiAlias(true);
        canvas.drawCircle(cx, cy, bounds.getWidth() * 0.28f, ring);
        canvas.drawCircle(cx, cy, bounds.getWidth() * 0.12f, solid(Color(255, 245, 203, 230)));
    } else if (motif == 1) {
        Paint glow = solid(Color(255, 236, 159, 210));
        canvas.drawCircle(bounds.getX() + bounds.getWidth() * 0.68f,
                          bounds.getY() + bounds.getHeight() * 0.34f,
                          bounds.getWidth() * 0.28f, glow);
        Paint horizon = solid(Color(14, 30, 44, 130));
        for (int i = 0; i < 5; ++i) {
            canvas.drawRoundRect(
                RectF(bounds.getX() + 30.0f,
                      bounds.getY() + 128.0f + static_cast<float>(i) * 25.0f,
                      bounds.getWidth() - 60.0f, 8.0f),
                4.0f, horizon);
        }
    } else if (motif == 2) {
        Paint ribbon = solid(Color(255, 255, 255, 90));
        for (int i = -2; i < 6; ++i) {
            canvas.save();
            canvas.translate(bounds.getX() + static_cast<float>(i) * 58.0f, bounds.getY());
            canvas.rotate(-0.32f);
            canvas.drawRoundRect(RectF(0.0f, -70.0f, 22.0f, bounds.getHeight() + 160.0f),
                                 11.0f, ribbon);
            canvas.restore();
        }
    } else {
        Paint dot = solid(Color(255, 255, 255, 205));
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 5; ++column) {
                const float radius = (row + column) % 2 == 0 ? 7.0f : 3.5f;
                canvas.drawCircle(bounds.getX() + 45.0f + column * 42.0f,
                                  bounds.getY() + 48.0f + row * 42.0f,
                                  radius, dot);
            }
        }
    }

    Paint sheen;
    sheen.setLinearGradient(bounds.getX(), bounds.getY(),
                            bounds.getX(), bounds.getY() + bounds.getHeight(),
                            Color(255, 255, 255, 42), Color(255, 255, 255, 0));
    canvas.drawRect(bounds, sheen);
    canvas.restore();
    strokeRoundRect(canvas, bounds, 24.0f, Color(255, 255, 255, 35), 1.5f);
}

void drawBackdrop(Canvas &canvas)
{
    Paint background;
    background.setLinearGradient(
        0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight),
        {
            Paint::ColorStop(0.0f, Color(10, 17, 28, 255)),
            Paint::ColorStop(0.46f, Color(18, 40, 51, 255)),
            Paint::ColorStop(0.74f, Color(54, 37, 74, 255)),
            Paint::ColorStop(1.0f, Color(21, 19, 34, 255)),
        });
    canvas.drawRect(RectF(0.0f, 0.0f, static_cast<float>(kWidth),
                          static_cast<float>(kHeight)), background);

    Paint topRule = solid(Color(255, 255, 255, 24));
    canvas.drawRect(RectF(64.0f, 100.0f, 1792.0f, 1.0f), topRule);
    canvas.drawText("WHATSCANVAS", 66.0f, 42.0f,
                    textStyle(28.0f, Color(247, 249, 252, 245)));
    canvas.drawText("CANVAS", 1510.0f, 47.0f,
                    textStyle(18.0f, Color(222, 231, 241, 155)));
    canvas.drawText("EFFECTS", 1610.0f, 47.0f,
                    textStyle(18.0f, Color(222, 231, 241, 155)));
    canvas.drawText("TEXT", 1730.0f, 47.0f,
                    textStyle(18.0f, Color(222, 231, 241, 155)));

    const RectF sidebar(64.0f, 136.0f, 260.0f, 832.0f);
    canvas.drawRoundRect(sidebar, 28.0f, solid(Color(5, 11, 20, 108)));
    strokeRoundRect(canvas, sidebar, 28.0f, Color(255, 255, 255, 25), 1.0f);
    canvas.drawText("NIGHT RADIO", 96.0f, 176.0f,
                    textStyle(17.0f, Color(155, 226, 218, 230)));
    canvas.drawText("A quiet set for", 96.0f, 222.0f,
                    textStyle(30.0f, Color(248, 249, 252, 238)));
    canvas.drawText("late hours.", 96.0f, 258.0f,
                    textStyle(30.0f, Color(248, 249, 252, 238)));

    const char *navItems[] = {"Discover", "Collections", "Favourites", "Recently played"};
    for (int i = 0; i < 4; ++i) {
        const float y = 350.0f + static_cast<float>(i) * 68.0f;
        if (i == 0) {
            canvas.drawRoundRect(RectF(84.0f, y - 14.0f, 220.0f, 48.0f), 16.0f,
                                 solid(Color(132, 219, 203, 38)));
            canvas.drawCircle(106.0f, y + 10.0f, 5.0f,
                              solid(Color(153, 238, 220, 245)));
        }
        canvas.drawText(navItems[i], 126.0f, y - 3.0f,
                        textStyle(20.0f, i == 0
                            ? Color(246, 250, 252, 242)
                            : Color(190, 202, 216, 165)));
    }

    canvas.drawText("PLAYING NEXT", 96.0f, 702.0f,
                    textStyle(15.0f, Color(183, 196, 211, 138)));
    drawAlbumCover(canvas, RectF(96.0f, 746.0f, 92.0f, 92.0f),
                   Color(31, 188, 183, 255), Color(82, 64, 160, 255), 0);
    canvas.drawText("Coastal Lines", 208.0f, 758.0f,
                    textStyle(19.0f, Color(245, 247, 251, 230)));
    canvas.drawText("Orchid FM", 208.0f, 790.0f,
                    textStyle(16.0f, Color(178, 192, 207, 155)));

    canvas.drawText("Night radio", 378.0f, 134.0f,
                    textStyle(56.0f, Color(250, 251, 253, 248)));
    canvas.drawText("A glass interface study", 382.0f, 202.0f,
                    textStyle(21.0f, Color(188, 205, 218, 165)));

    const RectF covers[] = {
        RectF(378.0f, 270.0f, 270.0f, 250.0f),
        RectF(674.0f, 270.0f, 270.0f, 250.0f),
        RectF(970.0f, 270.0f, 270.0f, 250.0f),
        RectF(1266.0f, 270.0f, 270.0f, 250.0f),
    };
    const Color coverStart[] = {
        Color(26, 189, 185, 255),
        Color(245, 111, 103, 255),
        Color(115, 88, 225, 255),
        Color(204, 225, 104, 255),
    };
    const Color coverEnd[] = {
        Color(38, 81, 155, 255),
        Color(166, 54, 132, 255),
        Color(48, 42, 106, 255),
        Color(33, 112, 120, 255),
    };
    for (int i = 0; i < 4; ++i) {
        drawAlbumCover(canvas, covers[i], coverStart[i], coverEnd[i], i);
    }

    canvas.drawText("Coastal Lines", 378.0f, 536.0f,
                    textStyle(20.0f, Color(242, 246, 249, 220)));
    canvas.drawText("Late Arrival", 674.0f, 536.0f,
                    textStyle(20.0f, Color(242, 246, 249, 220)));
    canvas.drawText("Afterglow", 970.0f, 536.0f,
                    textStyle(20.0f, Color(242, 246, 249, 220)));
    canvas.drawText("Sunday Static", 1266.0f, 536.0f,
                    textStyle(20.0f, Color(242, 246, 249, 220)));

    canvas.drawText("Recently played", 378.0f, 614.0f,
                    textStyle(24.0f, Color(243, 246, 250, 230)));
    const char *tracks[] = {"Silver Current", "Small Hours", "Neon Weather", "Parallel Skies"};
    const char *artists[] = {"Mira Vale", "Polar State", "Orchid FM", "Low Lanterns"};
    const char *durations[] = {"04:12", "03:48", "05:06", "03:31"};
    for (int i = 0; i < 4; ++i) {
        const float y = 666.0f + static_cast<float>(i) * 62.0f;
        canvas.drawCircle(392.0f, y + 12.0f, 4.0f,
                          solid(i == 1 ? Color(239, 123, 145, 230)
                                      : Color(184, 201, 214, 120)));
        canvas.drawText(tracks[i], 420.0f, y,
                        textStyle(18.0f, Color(236, 241, 246, 215)));
        canvas.drawText(artists[i], 790.0f, y,
                        textStyle(17.0f, Color(176, 192, 207, 145)));
        canvas.drawText(durations[i], 1490.0f, y,
                        textStyle(16.0f, Color(176, 192, 207, 145)));
        canvas.drawRect(RectF(378.0f, y + 43.0f, 1160.0f, 1.0f),
                        solid(Color(255, 255, 255, 20)));
    }
}

void beginGlass(Canvas &canvas, const RectF &bounds, float radius, float blurRadius,
                const Color &topTint, const Color &bottomTint)
{
    canvas.drawRoundRect(RectF(bounds.getX(), bounds.getY() + 18.0f,
                               bounds.getWidth(), bounds.getHeight()),
                         radius, solid(Color(0, 4, 12, 85)));

    LayerOptions options;
    options.setBackdropFilter(ImageFilter::blur(blurRadius));
    Paint composite = solid(Color(255, 255, 255, 255));
    canvas.saveLayer(bounds, composite, options);

    Path clip;
    clip.addRoundRect(bounds, radius);
    canvas.clipPath(clip);

    Paint tint;
    tint.setLinearGradient(bounds.getX(), bounds.getY(),
                           bounds.getX(), bounds.getY() + bounds.getHeight(),
                           topTint, bottomTint);
    canvas.drawRect(bounds, tint);

    Paint highlight;
    highlight.setLinearGradient(bounds.getX(), bounds.getY(),
                                bounds.getX(), bounds.getY() + 145.0f,
                                Color(255, 255, 255, 45), Color(255, 255, 255, 0));
    canvas.drawRect(RectF(bounds.getX(), bounds.getY(),
                          bounds.getWidth(), 150.0f), highlight);
    strokeRoundRect(canvas, bounds, radius, Color(255, 255, 255, 112), 1.5f);
}

void endGlass(Canvas &canvas)
{
    canvas.restore();
}

void drawPlayButton(Canvas &canvas, float cx, float cy, float radius)
{
    canvas.drawCircle(cx, cy, radius, solid(Color(241, 246, 247, 245)));
    Path play;
    play.moveTo(cx - 8.0f, cy - 13.0f);
    play.lineTo(cx + 15.0f, cy);
    play.lineTo(cx - 8.0f, cy + 13.0f);
    play.close();
    canvas.drawPath(play, solid(Color(20, 28, 38, 255)));
}

void drawNowPlaying(Canvas &canvas)
{
    const RectF panel(592.0f, 190.0f, 800.0f, 690.0f);
    beginGlass(canvas, panel, 46.0f, 30.0f,
               Color(31, 42, 55, 154), Color(18, 23, 35, 184));

    canvas.drawText("NOW PLAYING", 640.0f, 234.0f,
                    textStyle(15.0f, Color(165, 232, 220, 230)));
    canvas.drawText("LIVE / 02:14", 1224.0f, 234.0f,
                    textStyle(15.0f, Color(212, 221, 231, 155)));

    const RectF artwork(640.0f, 294.0f, 292.0f, 292.0f);
    drawAlbumCover(canvas, artwork, Color(111, 85, 226, 255),
                   Color(245, 96, 135, 255), 2);

    canvas.drawText("Afterglow", 982.0f, 310.0f,
                    textStyle(48.0f, Color(252, 252, 253, 248)));
    canvas.drawText("Orchid FM  /  Parallel Skies", 985.0f, 374.0f,
                    textStyle(19.0f, Color(211, 222, 231, 175)));
    canvas.drawText("A slow pulse for the final hour.", 985.0f, 430.0f,
                    textStyle(17.0f, Color(190, 205, 218, 145)));

    Paint waveform = solid(Color(173, 235, 220, 185));
    for (int i = 0; i < 22; ++i) {
        const float phase = static_cast<float>(i) * 0.72f;
        const float height = 14.0f + (std::sin(phase) + 1.0f) * 18.0f;
        canvas.drawRoundRect(RectF(985.0f + i * 14.0f, 515.0f - height * 0.5f,
                                   5.0f, height), 2.5f, waveform);
    }

    canvas.drawRoundRect(RectF(640.0f, 642.0f, 704.0f, 4.0f), 2.0f,
                         solid(Color(255, 255, 255, 38)));
    canvas.drawRoundRect(RectF(640.0f, 642.0f, 315.0f, 4.0f), 2.0f,
                         solid(Color(173, 235, 220, 230)));
    canvas.drawCircle(955.0f, 644.0f, 7.0f,
                      solid(Color(241, 249, 248, 245)));
    canvas.drawText("02:14", 640.0f, 664.0f,
                    textStyle(15.0f, Color(202, 214, 225, 155)));
    canvas.drawText("05:06", 1302.0f, 664.0f,
                    textStyle(15.0f, Color(202, 214, 225, 155)));

    Paint control = solid(Color(231, 238, 244, 210));
    canvas.drawRoundRect(RectF(860.0f, 756.0f, 34.0f, 4.0f), 2.0f, control);
    canvas.drawRoundRect(RectF(1090.0f, 756.0f, 34.0f, 4.0f), 2.0f, control);
    canvas.drawCircle(877.0f, 758.0f, 15.0f, control);
    canvas.drawCircle(1107.0f, 758.0f, 15.0f, control);
    drawPlayButton(canvas, 992.0f, 758.0f, 38.0f);

    endGlass(canvas);
}

void drawNotification(Canvas &canvas)
{
    const RectF notice(1450.0f, 156.0f, 390.0f, 178.0f);
    beginGlass(canvas, notice, 30.0f, 18.0f,
               Color(61, 52, 76, 148), Color(28, 27, 42, 176));
    canvas.drawCircle(1500.0f, 207.0f, 18.0f,
                      solid(Color(218, 235, 115, 235)));
    canvas.drawText("UP NEXT", 1532.0f, 181.0f,
                    textStyle(14.0f, Color(218, 235, 115, 220)));
    canvas.drawText("Sunday Static", 1490.0f, 228.0f,
                    textStyle(25.0f, Color(249, 249, 251, 240)));
    canvas.drawText("Low Lanterns  /  03:31", 1492.0f, 270.0f,
                    textStyle(16.0f, Color(206, 214, 224, 160)));
    endGlass(canvas);
}

void drawPlayerBar(Canvas &canvas)
{
    const RectF bar(378.0f, 914.0f, 1160.0f, 112.0f);
    beginGlass(canvas, bar, 32.0f, 42.0f,
               Color(29, 42, 51, 142), Color(17, 22, 32, 184));
    drawAlbumCover(canvas, RectF(402.0f, 930.0f, 80.0f, 80.0f),
                   Color(111, 85, 226, 255), Color(245, 96, 135, 255), 2);
    canvas.drawText("Afterglow", 504.0f, 942.0f,
                    textStyle(20.0f, Color(249, 250, 252, 235)));
    canvas.drawText("Orchid FM", 504.0f, 976.0f,
                    textStyle(16.0f, Color(191, 204, 216, 150)));
    drawPlayButton(canvas, 958.0f, 970.0f, 29.0f);
    canvas.drawRoundRect(RectF(1070.0f, 968.0f, 300.0f, 4.0f), 2.0f,
                         solid(Color(255, 255, 255, 42)));
    canvas.drawRoundRect(RectF(1070.0f, 968.0f, 188.0f, 4.0f), 2.0f,
                         solid(Color(173, 235, 220, 220)));
    canvas.drawText("02:14", 1396.0f, 958.0f,
                    textStyle(15.0f, Color(203, 214, 224, 155)));
    endGlass(canvas);
}

} // namespace

int main(int argc, char **argv)
{
    const std::string outputPath = argc > 1 ? argv[1] : "image-filter-showcase.png";

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

    canvas->beginFrame();
    drawBackdrop(*canvas);
    drawNowPlaying(*canvas);
    drawNotification(*canvas);
    drawPlayerBar(*canvas);
    canvas->endFrame();

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    GLint framebuffer = 0;
    GLint viewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    const Canvas::RenderStats stats = canvas->getRenderStats();
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
