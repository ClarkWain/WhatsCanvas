// AA showcase: renders the same scene twice — left with anti-aliasing OFF,
// right with Paint::setAntiAlias(true) — and writes a side-by-side PNG so the
// analytic anti-aliasing can be compared directly. MSAA is intentionally
// disabled so the image isolates WhatsCanvas' own analytic AA.

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include "wsc/wsc.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace wsc;

namespace {

constexpr int kPanelWidth = 460;
constexpr int kPanelHeight = 460;
constexpr int kWidth = kPanelWidth * 2;
constexpr int kHeight = kPanelHeight;
constexpr float kPi = 3.14159265358979323846f;

// Builds a 5-pointed concave star centred at (cx, cy).
Path makeStar(float cx, float cy, float outer, float inner)
{
    Path path;
    for (int i = 0; i < 10; ++i) {
        const float radius = (i % 2 == 0) ? outer : inner;
        const float angle = -kPi * 0.5f + static_cast<float>(i) * kPi / 5.0f;
        const float x = cx + std::cos(angle) * radius;
        const float y = cy + std::sin(angle) * radius;
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    path.close();
    return path;
}

// Draws the identical demo scene into the panel whose top-left is (ox, 0).
// The single toggle `aa` decides whether the paints opt into analytic AA.
void drawScene(Canvas &canvas, float ox, bool aa)
{
    // Concave star fill — exercises fill silhouette AA on sharp corners.
    Paint starPaint;
    starPaint.setStyle(Paint::Style::FILL);
    starPaint.setFillColor(Color(90, 170, 250));
    starPaint.setAntiAlias(aa);
    canvas.save();
    canvas.translate(ox + 150.0f, 130.0f);
    canvas.rotate(0.18f);
    canvas.drawPath(makeStar(0.0f, 0.0f, 92.0f, 38.0f), starPaint);
    canvas.restore();

    // Thin circle outline — exercises stroke AA.
    Paint ringPaint;
    ringPaint.setStyle(Paint::Style::STROKE);
    ringPaint.setStrokeColor(Color(255, 210, 90));
    ringPaint.setStrokeWidth(2.0f);
    ringPaint.setAntiAlias(aa);
    canvas.drawCircle(ox + 330.0f, 120.0f, 78.0f, ringPaint);

    // Fan of hairlines at many angles — exercises diagonal-edge AA.
    Paint linePaint;
    linePaint.setStyle(Paint::Style::STROKE);
    linePaint.setStrokeColor(Color(230, 230, 235));
    linePaint.setStrokeWidth(1.5f);
    linePaint.setStrokeCap(Paint::StrokeCap::ROUND);
    linePaint.setAntiAlias(aa);
    const float fanCx = ox + 110.0f;
    const float fanCy = 300.0f;
    for (int i = 0; i < 12; ++i) {
        const float angle = static_cast<float>(i) * kPi / 12.0f;
        const float len = 95.0f;
        canvas.drawLine(fanCx - std::cos(angle) * len, fanCy - std::sin(angle) * len,
                        fanCx + std::cos(angle) * len, fanCy + std::sin(angle) * len, linePaint);
    }

    // Rotated square outline — exercises mitred stroke joins under rotation.
    Paint squarePaint;
    squarePaint.setStyle(Paint::Style::STROKE);
    squarePaint.setStrokeColor(Color(120, 235, 170));
    squarePaint.setStrokeWidth(4.0f);
    squarePaint.setStrokeJoin(Paint::StrokeJoin::MITER);
    squarePaint.setAntiAlias(aa);
    canvas.save();
    canvas.translate(ox + 300.0f, 320.0f);
    canvas.rotate(0.5f);
    canvas.drawRect(RectF(-70.0f, -70.0f, 140.0f, 140.0f), squarePaint);
    canvas.restore();

    // Small filled triangle — exercises a rotated straight fill edge.
    Paint triPaint;
    triPaint.setStyle(Paint::Style::FILL);
    triPaint.setFillColor(Color(250, 120, 140));
    triPaint.setAntiAlias(aa);
    Path tri;
    tri.moveTo(ox + 360.0f, 250.0f);
    tri.lineTo(ox + 440.0f, 300.0f);
    tri.lineTo(ox + 350.0f, 410.0f);
    tri.close();
    canvas.drawPath(tri, triPaint);
}

// Multi-stop palette shared by both gradient panels.
struct GradStop { float t; Color color; };
const GradStop kGradStops[] = {
    {0.00f, Color(230, 60, 70)},
    {0.35f, Color(245, 210, 70)},
    {0.65f, Color(60, 200, 130)},
    {1.00f, Color(70, 120, 235)},
};

Color sampleStops(float t)
{
    t = std::min(std::max(t, 0.0f), 1.0f);
    const int count = static_cast<int>(sizeof(kGradStops) / sizeof(kGradStops[0]));
    for (int i = 1; i < count; ++i) {
        if (t <= kGradStops[i].t) {
            const GradStop &a = kGradStops[i - 1];
            const GradStop &b = kGradStops[i];
            const float span = std::max(b.t - a.t, 1e-4f);
            const float k = (t - a.t) / span;
            return Color(
                static_cast<int>(a.color.getR() + (b.color.getR() - a.color.getR()) * k),
                static_cast<int>(a.color.getG() + (b.color.getG() - a.color.getG()) * k),
                static_cast<int>(a.color.getB() + (b.color.getB() - a.color.getB()) * k));
        }
    }
    return kGradStops[count - 1].color;
}

std::vector<Paint::ColorStop> gradientColorStops()
{
    std::vector<Paint::ColorStop> stops;
    for (const GradStop &s : kGradStops) {
        stops.emplace_back(s.t, s.color);
    }
    return stops;
}

// Left panel: the gradient approximated by a handful of flat-colour bands —
// the banding/Mach-band artefacts you get from coarse vertex-colour (Gouraud)
// interpolation. Right panel: the same gradient evaluated per fragment.
void drawGradientScene(Canvas &canvas, float ox, bool fragmentLevel)
{
    const float x = ox + 40.0f;
    const float y = 60.0f;
    const float w = 380.0f;
    const float h = 150.0f;

    if (fragmentLevel) {
        Paint grad;
        grad.setStyle(Paint::Style::FILL);
        grad.setLinearGradient(x, 0.0f, x + w, 0.0f, gradientColorStops());
        canvas.drawRect(RectF(x, y, w, h), grad);
    } else {
        const int bands = 10; // coarse, like a low-vertex Gouraud mesh
        for (int i = 0; i < bands; ++i) {
            const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(bands);
            Paint band;
            band.setStyle(Paint::Style::FILL);
            band.setFillColor(sampleStops(t));
            const float bx = x + w * static_cast<float>(i) / static_cast<float>(bands);
            canvas.drawRect(RectF(bx, y, w / static_cast<float>(bands) + 1.0f, h), band);
        }
    }

    // Radial gradient below to show smooth vs stepped falloff.
    const float cx = ox + 230.0f;
    const float cy = 330.0f;
    const float radius = 110.0f;
    if (fragmentLevel) {
        Paint radial;
        radial.setStyle(Paint::Style::FILL);
        radial.setRadialGradient(cx, cy, radius, Color(250, 240, 210), Color(40, 60, 120));
        canvas.drawCircle(cx, cy, radius, radial);
    } else {
        const int rings = 8;
        for (int i = rings; i >= 1; --i) {
            const float t = static_cast<float>(i) / static_cast<float>(rings);
            Paint ring;
            ring.setStyle(Paint::Style::FILL);
            ring.setFillColor(Color(
                static_cast<int>(250 + (40 - 250) * t),
                static_cast<int>(240 + (60 - 240) * t),
                static_cast<int>(210 + (120 - 210) * t)));
            canvas.drawCircle(cx, cy, radius * t, ring);
        }
    }
}

// Draws filled shapes with a true separable-Gaussian drop shadow. The panel
// toggle picks the blur radius so the same shapes can be compared at two blurs.
void drawShadowScene(Canvas &canvas, float ox, bool strong)
{
    const float radius = strong ? 24.0f : 8.0f;
    const float dx = 6.0f;
    const float dy = 8.0f;
    const Color shadow(20, 20, 30, 150);

    {
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setFillColor(Color(90, 150, 235));
        p.setShadowLayer(radius, dx, dy, shadow);
        canvas.drawRoundRect(RectF(ox + 50.0f, 45.0f, 150.0f, 100.0f), 22.0f, p);
    }
    {
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setFillColor(Color(240, 120, 130));
        p.setShadowLayer(radius, dx, dy, shadow);
        canvas.drawCircle(ox + 330.0f, 95.0f, 55.0f, p);
    }
    {
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setFillColor(Color(250, 205, 70));
        p.setShadowLayer(radius, dx, dy, shadow);
        canvas.drawPath(makeStar(ox + 135.0f, 275.0f, 78.0f, 31.0f), p);
    }
    // Stroked outline: exercises the Gaussian shadow for stroke meshes.
    {
        Paint p;
        p.setStyle(Paint::Style::STROKE);
        p.setStrokeWidth(14.0f);
        p.setAntiAlias(true);
        p.setStrokeColor(Color(120, 210, 150));
        p.setShadowLayer(radius, dx, dy, shadow);
        canvas.drawRoundRect(RectF(ox + 265.0f, 210.0f, 150.0f, 120.0f), 26.0f, p);
    }
    // Geometry text: exercises the Gaussian shadow for glyph triangles.
    {
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setFillColor(Color(60, 70, 95));
        p.setTextSize(40.0f);
        p.setShadowLayer(radius, dx, dy, shadow);
        canvas.drawText("Shadow", ox + 60.0f, 380.0f, p);
    }
}

// Clips solid fills by non-rectangular paths. With the anti-aliased clip mask
// the star's diagonal edges and the circle's curve stay smooth; the old 1-bit
// stencil clip left them hard/jagged.
void drawClipScene(Canvas &canvas, float ox, bool strong)
{
    (void)strong;
    // Star-shaped clip over a solid fill.
    {
        canvas.save();
        canvas.clipPath(makeStar(ox + 150.0f, 150.0f, 120.0f, 50.0f));
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setFillColor(Color(90, 150, 235));
        canvas.drawRect(RectF(ox + 10.0f, 20.0f, 290.0f, 270.0f), p);
        canvas.restore();
    }
    // Circular clip over a solid fill.
    {
        Path circle;
        circle.addCircle(ox + 320.0f, 330.0f, 95.0f);
        canvas.save();
        canvas.clipPath(circle);
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setFillColor(Color(240, 120, 130));
        canvas.drawRect(RectF(ox + 210.0f, 230.0f, 220.0f, 200.0f), p);
        canvas.restore();
    }
    // Star clip intersected with the circle-clipped region is not needed here;
    // a rounded-rect clip shows a smooth mixed straight/curved edge.
    {
        Path roundish;
        roundish.addOval(RectF(ox + 40.0f, 300.0f, 150.0f, 120.0f));
        canvas.save();
        canvas.clipPath(roundish);
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setFillColor(Color(250, 205, 70));
        canvas.drawRect(RectF(ox + 20.0f, 290.0f, 200.0f, 150.0f), p);
        canvas.restore();
    }
}

std::string getEnv(const char *name)
{
#ifdef _MSC_VER
    char *value = nullptr;
    size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
        return std::string();
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

} // namespace

int main(int argc, char **argv)
{
    std::string outputPath = argc > 1 ? argv[1] : getEnv("WHATSCANVAS_AA_OUTPUT");
    if (outputPath.empty()) {
        outputPath = "aa_comparison.png";
    }

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 0);      // no MSAA — isolate analytic AA
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(kWidth, kHeight, "AA Showcase", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);

    if (!Canvas::loadOpenGL(reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        std::cerr << "Failed to load OpenGL" << std::endl;
        glfwTerminate();
        return 1;
    }

    int fbWidth = kWidth;
    int fbHeight = kHeight;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);

    {
        Canvas canvas;
        canvas.setSize(fbWidth, fbHeight);

        // Canvas::readPixelsRGBA already returns top-left-origin rows, so the
        // stb writer must NOT flip again (doing so produced upside-down images).

        // Renders `scene` into both panels and writes the framebuffer to `path`.
        auto renderAndSave = [&](const std::string &path,
                                 const std::function<void(Canvas &, float, bool)> &scene,
                                 const char *label,
                                 bool lightBackground = false) -> bool {
            canvas.beginFrame();
            if (lightBackground) {
                glClearColor(0.90f, 0.91f, 0.93f, 1.0f);
            } else {
                glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
            }
            glClear(GL_COLOR_BUFFER_BIT);

            scene(canvas, 0.0f, false);                          // left panel
            scene(canvas, static_cast<float>(kPanelWidth), true); // right panel

            Paint divider;
            divider.setStyle(Paint::Style::FILL);
            divider.setFillColor(lightBackground ? Color(150, 150, 155) : Color(0, 0, 0));
            canvas.drawRect(RectF(static_cast<float>(kPanelWidth) - 1.0f, 0.0f, 2.0f,
                                  static_cast<float>(kHeight)), divider);
            canvas.endFrame();

            std::vector<unsigned char> pixels;
            if (!canvas.readPixelsRGBA(pixels)
                || pixels.size() != static_cast<size_t>(fbWidth) * static_cast<size_t>(fbHeight) * 4) {
                std::cerr << "Pixel readback failed" << std::endl;
                return false;
            }
            if (stbi_write_png(path.c_str(), fbWidth, fbHeight, 4, pixels.data(), fbWidth * 4) == 0) {
                std::cerr << "Failed to write PNG: " << path << std::endl;
                return false;
            }
            std::cout << "Wrote " << path << " (" << fbWidth << "x" << fbHeight << ") — " << label << std::endl;
            return true;
        };

        // Derive a sibling path for the gradient image next to the AA image.
        std::string gradientPath = outputPath;
        const size_t slash = gradientPath.find_last_of("/\\");
        const std::string dir = (slash == std::string::npos) ? std::string() : gradientPath.substr(0, slash + 1);
        gradientPath = dir + "gradient_comparison.png";

        bool ok = renderAndSave(outputPath, drawScene, "left: AA off, right: AA on");
        ok = renderAndSave(gradientPath, drawGradientScene, "left: banded (Gouraud-style), right: fragment-level") && ok;
        const std::string shadowPath = dir + "shadow_comparison.png";
        ok = renderAndSave(shadowPath, drawShadowScene,
                           "true Gaussian shadow — left: radius 8, right: radius 24", true) && ok;
        const std::string clipPath = dir + "clip_comparison.png";
        ok = renderAndSave(clipPath, drawClipScene,
                           "anti-aliased path clipping (star / circle / oval masks)", true) && ok;
        if (!ok) {
            glfwTerminate();
            return 1;
        }
    }

    glfwTerminate();
    return 0;
}
