// Golden-image regression tests for the pure-CPU software backend.
//
// The software rasterizer is fully deterministic (no GPU, no driver variance),
// which makes it an ideal candidate for image-based regression testing in the
// style of Skia's GMs, Cairo's test suite, and ThorVG. Each canonical scene is
// rendered head-less through Canvas::createSoftware and compared, pixel by
// pixel (with a small tolerance for cross-compiler floating-point noise),
// against a committed reference image.
//
// Baselines live under tests/baselines/software/<scene>.pam as Netpbm PAM
// (P7, RGBA) so transparency is captured too. To (re)generate them after an
// intentional rendering change, run the executable with
//   WHATSCANVAS_UPDATE_SOFTWARE_BASELINES=1
// which rewrites every baseline instead of comparing. This keeps the workflow
// automated: no manual pixel inspection, no external tooling.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "wsc/wsc.h"

#ifndef WHATSCANVAS_SOFTWARE_BASELINE_DIR
#define WHATSCANVAS_SOFTWARE_BASELINE_DIR "."
#endif

using namespace wsc;

namespace {

std::unique_ptr<Canvas> makeSoftwareCanvas(int w, int h)
{
    auto c = Canvas::create(Canvas::Backend::Software, w, h);
    if (c) c->initializeContext();
    return c;
}

struct Bitmap
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba; // width*height*4, straight alpha
};

Bitmap readback(Canvas &canvas)
{
    Bitmap out;
    out.width = canvas.getWidth();
    out.height = canvas.getHeight();
    canvas.readPixelsRGBA(out.rgba);
    return out;
}

// --- Netpbm PAM (P7, RGBA) I/O -------------------------------------------

bool writePAM(const std::string &path, const Bitmap &img)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << "P7\n"
         << "WIDTH " << img.width << "\n"
         << "HEIGHT " << img.height << "\n"
         << "DEPTH 4\n"
         << "MAXVAL 255\n"
         << "TUPLTYPE RGB_ALPHA\n"
         << "ENDHDR\n";
    file.write(reinterpret_cast<const char *>(img.rgba.data()),
               static_cast<std::streamsize>(img.rgba.size()));
    return static_cast<bool>(file);
}

bool readPAM(const std::string &path, Bitmap &img)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::string token;
    file >> token;
    if (token != "P7") {
        return false;
    }
    int width = 0;
    int height = 0;
    int depth = 0;
    int maxval = 0;
    while (file >> token) {
        if (token == "ENDHDR") {
            break;
        } else if (token == "WIDTH") {
            file >> width;
        } else if (token == "HEIGHT") {
            file >> height;
        } else if (token == "DEPTH") {
            file >> depth;
        } else if (token == "MAXVAL") {
            file >> maxval;
        } else if (token == "TUPLTYPE") {
            file >> token; // consume the tuple type value
        } else {
            return false;
        }
    }
    if (width <= 0 || height <= 0 || depth != 4 || maxval != 255) {
        return false;
    }
    file.get(); // consume the single whitespace after ENDHDR
    img.width = width;
    img.height = height;
    img.rgba.assign(static_cast<std::size_t>(width) * height * 4u, 0);
    file.read(reinterpret_cast<char *>(img.rgba.data()),
              static_cast<std::streamsize>(img.rgba.size()));
    return file.gcount() == static_cast<std::streamsize>(img.rgba.size());
}

// --- Comparison ----------------------------------------------------------

// A regression is a real, visible change; small cross-compiler FP noise is not.
constexpr int kMaxChannelDelta = 12;   // per-channel tolerance
constexpr double kMaxMeanDelta = 1.0;  // average absolute channel error

bool compare(const std::string &name, const Bitmap &actual, const Bitmap &expected)
{
    if (actual.width != expected.width || actual.height != expected.height
        || actual.rgba.size() != expected.rgba.size()) {
        std::cerr << "FAILED: " << name << " - dimension mismatch (actual "
                  << actual.width << "x" << actual.height << ", expected "
                  << expected.width << "x" << expected.height << ")\n";
        return false;
    }
    int worst = 0;
    long long sum = 0;
    std::size_t worstIndex = 0;
    for (std::size_t i = 0; i < actual.rgba.size(); ++i) {
        const int delta = std::abs(static_cast<int>(actual.rgba[i]) - static_cast<int>(expected.rgba[i]));
        sum += delta;
        if (delta > worst) {
            worst = delta;
            worstIndex = i;
        }
    }
    const double mean = static_cast<double>(sum) / static_cast<double>(actual.rgba.size());
    if (worst > kMaxChannelDelta || mean > kMaxMeanDelta) {
        const std::size_t pixel = worstIndex / 4;
        std::cerr << "FAILED: " << name << " - image differs from baseline (max channel delta "
                  << worst << " at pixel (" << (pixel % actual.width) << "," << (pixel / actual.width)
                  << "), mean delta " << mean << "). Set WHATSCANVAS_UPDATE_SOFTWARE_BASELINES=1 to "
                     "regenerate if this change is intentional.\n";
        return false;
    }
    return true;
}

// --- Scenes --------------------------------------------------------------

// Two overlapping 50%-alpha rectangles whose union edge runs along a triangle
// diagonal; guards the top-left fill rule against double-compositing.
Bitmap sceneOverlapTranslucent()
{
    auto canvas = makeSoftwareCanvas(48, 48);
    canvas->beginFrame();
    Paint a;
    a.setStyle(Paint::Style::FILL);
    a.setAntiAlias(false);
    a.setColor(Color(255, 0, 0, 128));
    canvas->drawRect(RectF(6.0f, 6.0f, 24.0f, 24.0f), a);
    Paint b;
    b.setStyle(Paint::Style::FILL);
    b.setAntiAlias(false);
    b.setColor(Color(0, 0, 255, 128));
    canvas->drawRect(RectF(18.0f, 18.0f, 24.0f, 24.0f), b);
    canvas->endFrame();
    return readback(*canvas);
}

Bitmap sceneLinearGradient()
{
    auto canvas = makeSoftwareCanvas(64, 32);
    canvas->beginFrame();
    Paint grad;
    grad.setStyle(Paint::Style::FILL);
    grad.setAntiAlias(false);
    grad.setLinearGradient(0.0f, 0.0f, 64.0f, 0.0f,
                           {Paint::ColorStop(0.0f, Color(255, 0, 0, 255)),
                            Paint::ColorStop(0.5f, Color(0, 255, 0, 255)),
                            Paint::ColorStop(1.0f, Color(0, 0, 255, 255))});
    canvas->drawRect(RectF(0.0f, 0.0f, 64.0f, 32.0f), grad);
    canvas->endFrame();
    return readback(*canvas);
}

Bitmap sceneRadialGradient()
{
    auto canvas = makeSoftwareCanvas(48, 48);
    canvas->beginFrame();
    Paint grad;
    grad.setStyle(Paint::Style::FILL);
    grad.setAntiAlias(false);
    grad.setRadialGradient(24.0f, 24.0f, 22.0f,
                           {Paint::ColorStop(0.0f, Color(255, 255, 0, 255)),
                            Paint::ColorStop(1.0f, Color(255, 0, 128, 255))});
    canvas->drawRect(RectF(0.0f, 0.0f, 48.0f, 48.0f), grad);
    canvas->endFrame();
    return readback(*canvas);
}

Bitmap sceneClipCircleAA()
{
    auto canvas = makeSoftwareCanvas(48, 48);
    canvas->beginFrame();
    Path circle;
    circle.addCircle(24.0f, 24.0f, 18.0f);
    canvas->save();
    canvas->clipPath(circle);
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(0, 160, 255, 255));
    fill.setAntiAlias(true);
    canvas->drawRect(RectF(0.0f, 0.0f, 48.0f, 48.0f), fill);
    canvas->restore();
    canvas->endFrame();
    return readback(*canvas);
}

Bitmap sceneDropShadow()
{
    auto canvas = makeSoftwareCanvas(64, 64);
    canvas->beginFrame();
    Paint p;
    p.setStyle(Paint::Style::FILL);
    p.setFillColor(Color(255, 255, 255, 255));
    p.setAntiAlias(false);
    p.setShadowLayer(6.0f, 8.0f, 8.0f, Color(0, 0, 0, 200));
    canvas->drawRect(RectF(16.0f, 16.0f, 20.0f, 20.0f), p);
    canvas->endFrame();
    return readback(*canvas);
}

Bitmap sceneSaveLayer()
{
    auto canvas = makeSoftwareCanvas(48, 48);
    canvas->beginFrame();
    Paint bg;
    bg.setStyle(Paint::Style::FILL);
    bg.setAntiAlias(false);
    bg.setColor(Color(0, 128, 0, 255));
    canvas->drawRect(RectF(0.0f, 0.0f, 48.0f, 48.0f), bg);

    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255));
    layerPaint.setAlpha(128);
    canvas->saveLayer(RectF(8.0f, 8.0f, 32.0f, 32.0f), layerPaint);
    Paint red;
    red.setStyle(Paint::Style::FILL);
    red.setAntiAlias(false);
    red.setColor(Color(255, 0, 0, 255));
    canvas->drawRect(RectF(0.0f, 0.0f, 48.0f, 48.0f), red);
    canvas->restore();
    canvas->endFrame();
    return readback(*canvas);
}

Bitmap sceneBlendModes()
{
    auto canvas = makeSoftwareCanvas(64, 32);
    canvas->beginFrame();
    Paint bg;
    bg.setStyle(Paint::Style::FILL);
    bg.setAntiAlias(false);
    bg.setColor(Color(60, 60, 60, 255));
    canvas->drawRect(RectF(0.0f, 0.0f, 64.0f, 32.0f), bg);

    const Paint::BlendMode modes[3] = {Paint::BlendMode::ADD, Paint::BlendMode::MULTIPLY,
                                       Paint::BlendMode::SCREEN};
    for (int i = 0; i < 3; ++i) {
        Paint p;
        p.setStyle(Paint::Style::FILL);
        p.setAntiAlias(false);
        p.setColor(Color(200, 120, 40, 200));
        p.setBlendMode(modes[i]);
        canvas->drawRect(RectF(4.0f + i * 20.0f, 6.0f, 18.0f, 20.0f), p);
    }
    canvas->endFrame();
    return readback(*canvas);
}

Bitmap sceneImageTint()
{
    auto canvas = makeSoftwareCanvas(32, 32);

    std::vector<unsigned char> src(8 * 8 * 4, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * 8 + x) * 4;
            const bool checker = ((x / 2) + (y / 2)) % 2 == 0;
            src[i + 0] = checker ? 255 : 40;
            src[i + 1] = checker ? 255 : 40;
            src[i + 2] = checker ? 255 : 40;
            src[i + 3] = 255;
        }
    }
    Image image;
    image.loadFromRGBA(*canvas, src, 8, 8, false);

    canvas->beginFrame();
    Paint paint;
    paint.setColor(Color(120, 200, 255, 255)); // tint
    canvas->drawImage(image, RectF(4.0f, 4.0f, 24.0f, 24.0f), paint);
    canvas->endFrame();
    return readback(*canvas);
}

// Gamma-correct rendering: a 50%-alpha red rectangle over an opaque blue
// background. GL linearizes the source color and blends in linear space via
// GL_FRAMEBUFFER_SRGB, so the mid pixel differs from a straight-sRGB blend.
// This scene locks in the software backend's linear-space blend parity.
Bitmap sceneGammaSrcOver()
{
    Canvas::setGammaCorrect(true);
    auto canvas = makeSoftwareCanvas(48, 48);
    canvas->beginFrame();
    Paint bg;
    bg.setStyle(Paint::Style::FILL);
    bg.setAntiAlias(false);
    bg.setColor(Color(0, 0, 255, 255));
    canvas->drawRect(RectF(0.0f, 0.0f, 48.0f, 48.0f), bg);

    Paint over;
    over.setStyle(Paint::Style::FILL);
    over.setAntiAlias(false);
    over.setColor(Color(255, 0, 0, 128));
    canvas->drawRect(RectF(8.0f, 8.0f, 32.0f, 32.0f), over);
    canvas->endFrame();
    Bitmap out = readback(*canvas);
    Canvas::setGammaCorrect(false);
    return out;
}

struct Scene
{
    const char *name;
    Bitmap (*render)();
};

const Scene kScenes[] = {
    {"overlap_translucent", &sceneOverlapTranslucent},
    {"linear_gradient", &sceneLinearGradient},
    {"radial_gradient", &sceneRadialGradient},
    {"clip_circle_aa", &sceneClipCircleAA},
    {"drop_shadow", &sceneDropShadow},
    {"save_layer", &sceneSaveLayer},
    {"blend_modes", &sceneBlendModes},
    {"image_tint", &sceneImageTint},
    {"gamma_srcover", &sceneGammaSrcOver},
};

bool updateRequested()
{
    const char *env = std::getenv("WHATSCANVAS_UPDATE_SOFTWARE_BASELINES");
    return env != nullptr && env[0] != '\0' && !(env[0] == '0' && env[1] == '\0');
}

} // namespace

int main()
{
    const std::string baselineDir = WHATSCANVAS_SOFTWARE_BASELINE_DIR;
    const bool update = updateRequested();
    bool ok = true;

    for (const Scene &scene : kScenes) {
        const std::string path = baselineDir + "/" + scene.name + ".pam";
        const Bitmap actual = scene.render();
        if (actual.rgba.empty()) {
            std::cerr << "FAILED: " << scene.name << " - rendered an empty image\n";
            ok = false;
            continue;
        }

        if (update) {
            if (!writePAM(path, actual)) {
                std::cerr << "FAILED: could not write baseline " << path << "\n";
                ok = false;
            } else {
                std::cout << "updated baseline: " << path << "\n";
            }
            continue;
        }

        Bitmap expected;
        if (!readPAM(path, expected)) {
            std::cerr << "FAILED: " << scene.name << " - missing or unreadable baseline " << path
                      << " (run with WHATSCANVAS_UPDATE_SOFTWARE_BASELINES=1 to create it)\n";
            ok = false;
            continue;
        }
        if (!compare(scene.name, actual, expected)) {
            ok = false;
        }
    }

    if (update) {
        std::cout << "Baselines regenerated. Re-run without the env var to verify.\n";
        return ok ? 0 : 1;
    }
    if (ok) {
        std::cout << "All software golden-image scenes match their baselines.\n";
    }
    return ok ? 0 : 1;
}
