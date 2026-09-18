// Software vs OpenGL parity probe.
//
// Runs a curated set of small scenes against both the Software and OpenGL
// backends, reads pixels back, compares them and prints a machine-readable
// summary. Also dumps `<scene>.software.ppm`, `<scene>.opengl.ppm` and
// `<scene>.diff.ppm` for every scene into the working directory (or the
// directory passed via `--out <dir>`).
//
// Output columns per scene:
//   scene=<id> w=<w> h=<h> max=<max_channel_delta> mean=<mean_delta>
//   bad4=<ratio_at_thresh_4> bad16=<ratio_at_thresh_16> bad32=<ratio_at_thresh_32>
//
// A "bad" pixel is any pixel where at least one of the four channels differs by
// more than the threshold.
//
// Usage:
//   parity_probe                       # runs all scenes into ./parity_probe_out/
//   parity_probe --out my/dir          # writes into my/dir
//   parity_probe --only rect_fill_aa   # runs a single scene id
//   parity_probe --size 320            # renders at 320x320 (default 256)
//   parity_probe --require-gl          # fail if OpenGL context cannot be created
//
// This tool intentionally does NOT touch text rendering — freetype/DirectWrite
// glyph rasterizers are known-different by design and are covered by
// dedicated text tests.

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <wsc/wsc.h>

namespace {

struct Args
{
    std::string outDir = "parity_probe_out";
    std::string only;
    int size = 256;
    bool requireGL = false;
};

bool parseArgs(int argc, char **argv, Args &args)
{
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) {
            args.outDir = argv[++i];
        } else if (a == "--only" && i + 1 < argc) {
            args.only = argv[++i];
        } else if (a == "--size" && i + 1 < argc) {
            args.size = std::atoi(argv[++i]);
            if (args.size < 32 || args.size > 4096) {
                std::cerr << "PARITY_PROBE bad --size value\n";
                return false;
            }
        } else if (a == "--require-gl") {
            args.requireGL = true;
        } else if (a == "-h" || a == "--help") {
            std::cout <<
                "parity_probe [--out DIR] [--only SCENE_ID] [--size N] [--require-gl]\n";
            return false;
        } else {
            std::cerr << "PARITY_PROBE unknown arg: " << a << "\n";
            return false;
        }
    }
    return true;
}

std::vector<unsigned char> makeCheckerImage(int w, int h)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool checker = ((x / 4) + (y / 4)) % 2 == 0;
            const size_t o = (static_cast<size_t>(y) * w + x) * 4;
            if (checker) {
                pixels[o + 0] = 240; pixels[o + 1] = 90; pixels[o + 2] = 60; pixels[o + 3] = 255;
            } else {
                pixels[o + 0] = 30; pixels[o + 1] = 180; pixels[o + 2] = 230; pixels[o + 3] = 255;
            }
        }
    }
    return pixels;
}

// ------------------------------ Scenes ------------------------------

using SceneFn = std::function<void(wsc::Canvas &)>;

struct Scene
{
    std::string id;
    SceneFn draw;
    const char *notes;
};

void sceneClearOnly(wsc::Canvas &c)
{
    c.drawColor(wsc::Color(30, 40, 80, 255));
}

void sceneRectFillAA(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p; p.setAntiAlias(true); p.setColor(wsc::Color(40, 120, 240, 255));
    c.drawRect(wsc::RectF(20.5f, 20.5f, 160.0f, 100.0f), p);
}

void sceneRectFillNoAA(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p; p.setAntiAlias(false); p.setColor(wsc::Color(40, 120, 240, 255));
    c.drawRect(wsc::RectF(20.5f, 20.5f, 160.0f, 100.0f), p);
}

void sceneCircleFill(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p; p.setAntiAlias(true); p.setColor(wsc::Color(220, 60, 80, 255));
    c.drawCircle(128.0f, 128.0f, 90.0f, p);
}

void sceneRoundRect(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p; p.setAntiAlias(true); p.setColor(wsc::Color(80, 160, 90, 255));
    c.drawRoundRect(wsc::RectF(24.0f, 32.0f, 200.0f, 180.0f), 32.0f, p);
}

void sceneOval(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p; p.setAntiAlias(true); p.setColor(wsc::Color(200, 100, 200, 255));
    c.drawOval(wsc::RectF(20.0f, 60.0f, 216.0f, 128.0f), p);
}

void sceneStrokeRect(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setStyle(wsc::Paint::Style::STROKE);
    p.setStrokeWidth(6.0f);
    p.setColor(wsc::Color(10, 10, 10, 255));
    c.drawRect(wsc::RectF(30.5f, 30.5f, 180.0f, 180.0f), p);
}

void sceneStrokeCircle(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setStyle(wsc::Paint::Style::STROKE);
    p.setStrokeWidth(4.0f);
    p.setColor(wsc::Color(10, 10, 10, 255));
    c.drawCircle(128.0f, 128.0f, 80.0f, p);
}

void sceneThinLine(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setStyle(wsc::Paint::Style::STROKE);
    p.setStrokeWidth(1.0f);
    p.setColor(wsc::Color(10, 10, 10, 255));
    for (int i = 0; i < 8; ++i) {
        const float t = static_cast<float>(i) / 7.0f;
        c.drawLine(20.0f, 24.0f + t * 200.0f, 236.0f, 24.0f + t * 200.0f, p);
    }
}

void sceneThickLineCaps(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    for (int i = 0; i < 3; ++i) {
        wsc::Paint p;
        p.setAntiAlias(true);
        p.setStyle(wsc::Paint::Style::STROKE);
        p.setStrokeWidth(18.0f);
        p.setColor(wsc::Color(20, 20, 20, 255));
        p.setStrokeCap(i == 0 ? wsc::Paint::StrokeCap::BUTT
                    : i == 1 ? wsc::Paint::StrokeCap::ROUND
                             : wsc::Paint::StrokeCap::SQUARE);
        const float y = 60.0f + i * 60.0f;
        c.drawLine(40.0f, y, 216.0f, y, p);
    }
}

void sceneDiagonalLine(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setStyle(wsc::Paint::Style::STROKE);
    p.setStrokeWidth(2.0f);
    p.setColor(wsc::Color(10, 10, 10, 255));
    for (int i = 0; i < 10; ++i) {
        const float t = static_cast<float>(i) / 9.0f;
        c.drawLine(20.0f + t * 216.0f, 20.0f, 20.0f, 20.0f + t * 216.0f, p);
    }
}

void sceneLinearGradient(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setLinearGradient(0.0f, 0.0f, 256.0f, 0.0f,
                        wsc::Color(255, 40, 80, 255),
                        wsc::Color(40, 100, 240, 255));
    c.drawRect(wsc::RectF(0.0f, 32.0f, 256.0f, 192.0f), p);
}

void sceneLinearGradientDiag(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setLinearGradient(0.0f, 0.0f, 256.0f, 256.0f,
                        wsc::Color(20, 30, 60, 255),
                        wsc::Color(240, 220, 80, 255));
    c.drawRect(wsc::RectF(0.0f, 0.0f, 256.0f, 256.0f), p);
}

void sceneRadialGradient(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setRadialGradient(128.0f, 128.0f, 110.0f,
                        wsc::Color(255, 220, 80, 255),
                        wsc::Color(40, 60, 100, 255));
    c.drawRect(wsc::RectF(0.0f, 0.0f, 256.0f, 256.0f), p);
}

void sceneShadowLayer(wsc::Canvas &c)
{
    c.drawColor(wsc::Color(240, 240, 245, 255));
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setColor(wsc::Color(255, 255, 255, 255));
    p.setShadowLayer(12.0f, 0.0f, 8.0f, wsc::Color(0, 0, 0, 160));
    c.drawRoundRect(wsc::RectF(48.0f, 48.0f, 160.0f, 160.0f), 20.0f, p);
}

void sceneBoxShadow(wsc::Canvas &c)
{
    c.drawColor(wsc::Color(240, 240, 245, 255));
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setColor(wsc::Color(255, 255, 255, 255));
    c.drawBoxShadow(wsc::RectF(48.0f, 48.0f, 160.0f, 160.0f), 24.0f, 0.0f, 16.0f,
                    0.0f, 8.0f, wsc::Color(0, 0, 0, 140));
    c.drawRoundRect(wsc::RectF(48.0f, 48.0f, 160.0f, 160.0f), 24.0f, p);
}

void sceneClipRect(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    c.save();
    c.clipRect(wsc::RectF(40.0f, 40.0f, 160.0f, 160.0f));
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setLinearGradient(0.0f, 0.0f, 256.0f, 256.0f,
                        wsc::Color(200, 60, 60, 255),
                        wsc::Color(40, 60, 200, 255));
    c.drawRect(wsc::RectF(0.0f, 0.0f, 256.0f, 256.0f), p);
    c.restore();
}

void sceneClipPathCircle(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    c.save();
    wsc::Path clip;
    clip.addCircle(128.0f, 128.0f, 90.0f);
    c.clipPath(clip);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setLinearGradient(0.0f, 0.0f, 256.0f, 256.0f,
                        wsc::Color(200, 60, 60, 255),
                        wsc::Color(40, 60, 200, 255));
    c.drawRect(wsc::RectF(0.0f, 0.0f, 256.0f, 256.0f), p);
    c.restore();
}

void sceneNestedClipTransform(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    c.save();
    c.translate(128.0f, 128.0f);
    c.rotate(0.35f);
    c.scale(1.0f, 0.6f);
    c.clipRect(wsc::RectF(-90.0f, -60.0f, 180.0f, 120.0f));
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setColor(wsc::Color(40, 160, 220, 255));
    c.drawCircle(0.0f, 0.0f, 120.0f, p);
    c.restore();
}

void sceneSaveLayerAlpha(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint layerPaint;
    layerPaint.setAlpha(128);
    c.saveLayer(wsc::RectF(0.0f, 0.0f, 256.0f, 256.0f), layerPaint);
    wsc::Paint a; a.setAntiAlias(true); a.setColor(wsc::Color(255, 0, 0, 255));
    c.drawCircle(96.0f, 128.0f, 64.0f, a);
    wsc::Paint b; b.setAntiAlias(true); b.setColor(wsc::Color(0, 128, 255, 255));
    c.drawCircle(160.0f, 128.0f, 64.0f, b);
    c.restore();
}

void sceneBlend(wsc::Canvas &c, wsc::Paint::BlendMode mode)
{
    c.drawColor(wsc::Color(240, 240, 245, 255));
    wsc::Paint back; back.setAntiAlias(true);
    back.setColor(wsc::Color(255, 190, 40, 255));
    c.drawRect(wsc::RectF(20.0f, 40.0f, 216.0f, 176.0f), back);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setColor(wsc::Color(40, 120, 240, 200));
    p.setBlendMode(mode);
    c.drawCircle(128.0f, 128.0f, 80.0f, p);
}

void sceneImageNearest(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Image image;
    auto pixels = makeCheckerImage(16, 16);
    c.loadImageFromRGBA(image, pixels, 16, 16);
    wsc::Paint p;
    p.setImageSampling(wsc::Paint::ImageSampling::NEAREST);
    c.drawImage(image, wsc::RectF(24.0f, 24.0f, 208.0f, 208.0f), p);
}

void sceneImageLinear(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Image image;
    auto pixels = makeCheckerImage(16, 16);
    c.loadImageFromRGBA(image, pixels, 16, 16);
    wsc::Paint p;
    p.setImageSampling(wsc::Paint::ImageSampling::LINEAR);
    c.drawImage(image, wsc::RectF(24.0f, 24.0f, 208.0f, 208.0f), p);
}

void sceneImageTint(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Image image;
    auto pixels = makeCheckerImage(16, 16);
    c.loadImageFromRGBA(image, pixels, 16, 16);
    wsc::Paint p;
    p.setImageSampling(wsc::Paint::ImageSampling::LINEAR);
    p.setColor(wsc::Color(120, 200, 255, 180));
    c.drawImage(image, wsc::RectF(24.0f, 24.0f, 208.0f, 208.0f), p);
}

void sceneComplexPath(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Path star;
    const float cx = 128.0f, cy = 128.0f;
    const float outer = 100.0f, inner = 42.0f;
    for (int i = 0; i < 10; ++i) {
        const float ang = -1.5707963f + i * 3.14159265f / 5.0f;
        const float r = (i % 2 == 0) ? outer : inner;
        const float x = cx + std::cos(ang) * r;
        const float y = cy + std::sin(ang) * r;
        if (i == 0) star.moveTo(x, y); else star.lineTo(x, y);
    }
    star.close();
    wsc::Paint p; p.setAntiAlias(true); p.setColor(wsc::Color(230, 70, 50, 255));
    c.drawPath(star, p);
}

void sceneComplexPathEvenOdd(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Path pth;
    pth.setFillType(wsc::Path::FillType::EVEN_ODD);
    pth.addCircle(128.0f, 128.0f, 100.0f);
    pth.addCircle(128.0f, 128.0f, 60.0f);
    pth.addCircle(128.0f, 128.0f, 20.0f);
    wsc::Paint p; p.setAntiAlias(true); p.setColor(wsc::Color(60, 140, 220, 255));
    c.drawPath(pth, p);
}

void sceneBezier(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Path pth;
    pth.moveTo(30.0f, 200.0f);
    pth.cubicTo(60.0f, 20.0f, 200.0f, 20.0f, 226.0f, 200.0f);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setStyle(wsc::Paint::Style::STROKE);
    p.setStrokeWidth(6.0f);
    p.setStrokeCap(wsc::Paint::StrokeCap::ROUND);
    p.setColor(wsc::Color(20, 20, 20, 255));
    c.drawPath(pth, p);
}

void sceneStrokeJoins(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    const wsc::Paint::StrokeJoin joins[3] = {
        wsc::Paint::StrokeJoin::MITER,
        wsc::Paint::StrokeJoin::ROUND,
        wsc::Paint::StrokeJoin::BEVEL,
    };
    for (int i = 0; i < 3; ++i) {
        wsc::Path pth;
        const float x0 = 30.0f + i * 70.0f;
        pth.moveTo(x0, 200.0f);
        pth.lineTo(x0 + 30.0f, 60.0f);
        pth.lineTo(x0 + 60.0f, 200.0f);
        wsc::Paint p;
        p.setAntiAlias(true);
        p.setStyle(wsc::Paint::Style::STROKE);
        p.setStrokeWidth(12.0f);
        p.setStrokeJoin(joins[i]);
        p.setColor(wsc::Color(20, 20, 20, 255));
        c.drawPath(pth, p);
    }
}

void sceneDashedStroke(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setStyle(wsc::Paint::Style::STROKE);
    p.setStrokeWidth(4.0f);
    p.setColor(wsc::Color(20, 20, 20, 255));
    p.setDashPathEffect({12.0f, 8.0f}, 0.0f);
    c.drawLine(20.0f, 128.0f, 236.0f, 128.0f, p);
    p.setDashPathEffect({4.0f, 4.0f}, 0.0f);
    c.drawLine(20.0f, 172.0f, 236.0f, 172.0f, p);
    p.setDashPathEffect({20.0f, 4.0f, 4.0f, 4.0f}, 0.0f);
    c.drawLine(20.0f, 88.0f, 236.0f, 88.0f, p);
}

void sceneArcPie(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setColor(wsc::Color(220, 90, 60, 255));
    c.drawArc(wsc::RectF(40.0f, 40.0f, 176.0f, 176.0f), 0.0f, 4.7f, true, p);
}

void sceneArcOpen(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint p;
    p.setAntiAlias(true);
    p.setStyle(wsc::Paint::Style::STROKE);
    p.setStrokeWidth(10.0f);
    p.setColor(wsc::Color(60, 100, 200, 255));
    c.drawArc(wsc::RectF(40.0f, 40.0f, 176.0f, 176.0f), 0.5f, 4.0f, false, p);
}

void sceneGaussianBlurLayer(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Paint layerPaint;
    wsc::LayerOptions opt;
    opt.setImageFilter(wsc::ImageFilter::blur(6.0f, 6.0f));
    c.saveLayer(wsc::RectF(16.0f, 16.0f, 224.0f, 224.0f), layerPaint, opt);
    wsc::Paint a; a.setAntiAlias(true); a.setColor(wsc::Color(255, 60, 60, 255));
    c.drawCircle(96.0f, 96.0f, 44.0f, a);
    wsc::Paint b; b.setAntiAlias(true); b.setColor(wsc::Color(60, 60, 255, 255));
    c.drawCircle(160.0f, 160.0f, 44.0f, b);
    c.restore();
}

void sceneBackdropBlur(wsc::Canvas &c)
{
    // background
    wsc::Paint bg;
    bg.setLinearGradient(0.0f, 0.0f, 256.0f, 0.0f,
                         wsc::Color(255, 90, 40, 255),
                         wsc::Color(40, 90, 255, 255));
    c.drawRect(wsc::RectF(0.0f, 0.0f, 256.0f, 256.0f), bg);
    for (int i = 0; i < 8; ++i) {
        wsc::Paint p; p.setAntiAlias(false);
        p.setColor((i % 2 == 0) ? wsc::Color(255, 255, 255, 220)
                                 : wsc::Color(20, 20, 20, 220));
        c.drawRect(wsc::RectF(0.0f, static_cast<float>(i * 32), 256.0f, 16.0f), p);
    }
    wsc::Paint composite;
    wsc::LayerOptions opt;
    opt.setBackdropFilter(wsc::ImageFilter::blur(8.0f, 8.0f));
    c.saveLayer(wsc::RectF(48.0f, 48.0f, 160.0f, 160.0f), composite, opt);
    wsc::Paint tint;
    tint.setColor(wsc::Color(255, 255, 255, 60));
    c.drawRect(wsc::RectF(48.0f, 48.0f, 160.0f, 160.0f), tint);
    c.restore();
}

void sceneInnerShadow(wsc::Canvas &c)
{
    c.drawColor(wsc::Color(240, 240, 245, 255));
    wsc::Paint composite;
    wsc::LayerOptions opt;
    opt.setImageFilter(wsc::ImageFilter::innerShadow(6.0f, 4.0f, 4.0f, 4.0f,
                       wsc::Color(0, 0, 0, 200)));
    c.saveLayer(wsc::RectF(40.0f, 40.0f, 176.0f, 176.0f), composite, opt);
    wsc::Paint p; p.setAntiAlias(true); p.setColor(wsc::Color(220, 230, 240, 255));
    c.drawRect(wsc::RectF(40.0f, 40.0f, 176.0f, 176.0f), p);
    c.restore();
}

void sceneColorMatrix(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    wsc::Image image;
    auto pixels = makeCheckerImage(16, 16);
    c.loadImageFromRGBA(image, pixels, 16, 16);
    wsc::Paint p;
    p.setImageSampling(wsc::Paint::ImageSampling::LINEAR);
    // Simple grayscale color matrix.
    std::array<float, 20> m = {
        0.299f, 0.587f, 0.114f, 0.0f, 0.0f,
        0.299f, 0.587f, 0.114f, 0.0f, 0.0f,
        0.299f, 0.587f, 0.114f, 0.0f, 0.0f,
        0.000f, 0.000f, 0.000f, 1.0f, 0.0f,
    };
    p.setColorMatrix(m);
    c.drawImage(image, wsc::RectF(24.0f, 24.0f, 208.0f, 208.0f), p);
}

void sceneTransformStack(wsc::Canvas &c)
{
    c.drawColor(wsc::Color::WHITE);
    for (int i = 0; i < 5; ++i) {
        c.save();
        c.translate(128.0f, 128.0f);
        c.rotate(i * 0.42f);
        c.translate(80.0f, 0.0f);
        wsc::Paint p; p.setAntiAlias(true);
        p.setColor(wsc::Color(40 + i * 40, 60, 220 - i * 40, 200));
        c.drawRoundRect(wsc::RectF(-24.0f, -12.0f, 48.0f, 24.0f), 6.0f, p);
        c.restore();
    }
}

std::vector<Scene> buildScenes()
{
    return {
        {"clear_only", sceneClearOnly, "solid clear"},
        {"rect_fill_aa", sceneRectFillAA, "AA rect fill"},
        {"rect_fill_noaa", sceneRectFillNoAA, "non-AA rect fill"},
        {"circle_fill", sceneCircleFill, "AA circle fill"},
        {"round_rect", sceneRoundRect, "AA round rect"},
        {"oval", sceneOval, "AA oval"},
        {"stroke_rect", sceneStrokeRect, "stroked rect"},
        {"stroke_circle", sceneStrokeCircle, "stroked circle"},
        {"thin_lines", sceneThinLine, "1px stroke lines"},
        {"thick_line_caps", sceneThickLineCaps, "BUTT/ROUND/SQUARE caps"},
        {"diagonal_lines", sceneDiagonalLine, "2px diagonal strokes"},
        {"linear_gradient", sceneLinearGradient, "horizontal linear gradient"},
        {"linear_gradient_diag", sceneLinearGradientDiag, "diagonal linear gradient"},
        {"radial_gradient", sceneRadialGradient, "radial gradient"},
        {"shadow_layer", sceneShadowLayer, "Paint.setShadowLayer"},
        {"box_shadow", sceneBoxShadow, "drawBoxShadow"},
        {"clip_rect", sceneClipRect, "clipRect + gradient"},
        {"clip_path_circle", sceneClipPathCircle, "clipPath circle + gradient"},
        {"nested_clip_xform", sceneNestedClipTransform, "clip + rotate + scale"},
        {"save_layer_alpha", sceneSaveLayerAlpha, "saveLayer with alpha=128"},
        {"blend_src_over",  [](wsc::Canvas &c){ sceneBlend(c, wsc::Paint::BlendMode::SRC_OVER); }, "SRC_OVER"},
        {"blend_multiply",  [](wsc::Canvas &c){ sceneBlend(c, wsc::Paint::BlendMode::MULTIPLY); }, "MULTIPLY"},
        {"blend_screen",    [](wsc::Canvas &c){ sceneBlend(c, wsc::Paint::BlendMode::SCREEN); }, "SCREEN"},
        {"blend_xor",       [](wsc::Canvas &c){ sceneBlend(c, wsc::Paint::BlendMode::XOR); }, "XOR"},
        {"blend_add",       [](wsc::Canvas &c){ sceneBlend(c, wsc::Paint::BlendMode::ADD); }, "ADD"},
        {"blend_src_in",    [](wsc::Canvas &c){ sceneBlend(c, wsc::Paint::BlendMode::SRC_IN); }, "SRC_IN"},
        {"blend_dst_in",    [](wsc::Canvas &c){ sceneBlend(c, wsc::Paint::BlendMode::DST_IN); }, "DST_IN"},
        {"blend_src_out",   [](wsc::Canvas &c){ sceneBlend(c, wsc::Paint::BlendMode::SRC_OUT); }, "SRC_OUT"},
        {"blend_dst_out",   [](wsc::Canvas &c){ sceneBlend(c, wsc::Paint::BlendMode::DST_OUT); }, "DST_OUT"},
        {"blend_src_atop",  [](wsc::Canvas &c){ sceneBlend(c, wsc::Paint::BlendMode::SRC_ATOP); }, "SRC_ATOP"},
        {"blend_dst_atop",  [](wsc::Canvas &c){ sceneBlend(c, wsc::Paint::BlendMode::DST_ATOP); }, "DST_ATOP"},
        {"image_nearest", sceneImageNearest, "drawImage nearest"},
        {"image_linear", sceneImageLinear, "drawImage linear"},
        {"image_tint_alpha", sceneImageTint, "drawImage tinted+alpha"},
        {"complex_path_star", sceneComplexPath, "star polygon"},
        {"complex_path_evenodd", sceneComplexPathEvenOdd, "concentric circles even-odd"},
        {"bezier_stroke", sceneBezier, "cubic bezier stroke"},
        {"stroke_joins", sceneStrokeJoins, "MITER/ROUND/BEVEL joins"},
        {"dashed_stroke", sceneDashedStroke, "dashed line effect"},
        {"arc_pie", sceneArcPie, "arc with center"},
        {"arc_open", sceneArcOpen, "stroked open arc"},
        {"gaussian_blur_layer", sceneGaussianBlurLayer, "saveLayer + blur"},
        {"backdrop_blur", sceneBackdropBlur, "saveLayer + backdrop blur"},
        {"inner_shadow_layer", sceneInnerShadow, "saveLayer + inner shadow"},
        {"color_matrix", sceneColorMatrix, "image + color matrix"},
        {"transform_stack", sceneTransformStack, "translate/rotate/scale stack"},
    };
}

// ------------------------------ Diff & IO ------------------------------

struct DiffStats
{
    int maxDelta = 0;
    double meanDelta = 0.0;
    double badPixel4 = 0.0;
    double badPixel16 = 0.0;
    double badPixel32 = 0.0;
    int worstX = 0;
    int worstY = 0;
    int worstChannel = 0;
};

DiffStats diffImages(const std::vector<unsigned char> &a,
                     const std::vector<unsigned char> &b,
                     int w, int h)
{
    DiffStats s;
    if (a.size() != b.size() || a.empty()) {
        s.maxDelta = 255;
        s.meanDelta = 255.0;
        s.badPixel4 = s.badPixel16 = s.badPixel32 = 1.0;
        return s;
    }
    const size_t pixelCount = static_cast<size_t>(w) * h;
    std::uint64_t total = 0;
    size_t bad4 = 0, bad16 = 0, bad32 = 0;
    for (size_t p = 0; p < pixelCount; ++p) {
        int worstThisPixel = 0;
        for (int ch = 0; ch < 4; ++ch) {
            const size_t idx = p * 4 + ch;
            const int d = std::abs(static_cast<int>(a[idx]) - static_cast<int>(b[idx]));
            total += static_cast<std::uint64_t>(d);
            if (d > worstThisPixel) worstThisPixel = d;
            if (d > s.maxDelta) {
                s.maxDelta = d;
                s.worstX = static_cast<int>(p % w);
                s.worstY = static_cast<int>(p / w);
                s.worstChannel = ch;
            }
        }
        if (worstThisPixel > 4) ++bad4;
        if (worstThisPixel > 16) ++bad16;
        if (worstThisPixel > 32) ++bad32;
    }
    s.meanDelta = static_cast<double>(total) / static_cast<double>(pixelCount * 4);
    s.badPixel4 = static_cast<double>(bad4) / static_cast<double>(pixelCount);
    s.badPixel16 = static_cast<double>(bad16) / static_cast<double>(pixelCount);
    s.badPixel32 = static_cast<double>(bad32) / static_cast<double>(pixelCount);
    return s;
}

bool writePPM(const std::string &path,
              const std::vector<unsigned char> &pixels, int w, int h)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << w << " " << h << "\n255\n";
    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        rgb[i * 3 + 0] = pixels[i * 4 + 0];
        rgb[i * 3 + 1] = pixels[i * 4 + 1];
        rgb[i * 3 + 2] = pixels[i * 4 + 2];
    }
    f.write(reinterpret_cast<const char *>(rgb.data()),
            static_cast<std::streamsize>(rgb.size()));
    return f.good();
}

bool writeDiffPPM(const std::string &path,
                  const std::vector<unsigned char> &a,
                  const std::vector<unsigned char> &b,
                  int w, int h)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << w << " " << h << "\n255\n";
    std::vector<unsigned char> rgb(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        int worst = 0;
        for (int ch = 0; ch < 4; ++ch) {
            const int d = std::abs(static_cast<int>(a[i * 4 + ch])
                                   - static_cast<int>(b[i * 4 + ch]));
            if (d > worst) worst = d;
        }
        // Amplify small differences: >0 => gray..red gradient (saturating).
        int amp = std::min(255, worst * 6);
        rgb[i * 3 + 0] = static_cast<unsigned char>(amp);
        rgb[i * 3 + 1] = 0;
        rgb[i * 3 + 2] = 0;
    }
    f.write(reinterpret_cast<const char *>(rgb.data()),
            static_cast<std::streamsize>(rgb.size()));
    return f.good();
}

bool renderScene(wsc::Canvas::Backend backend, int size, const Scene &scene,
                 std::vector<unsigned char> &pixels)
{
    auto canvas = wsc::Canvas::create(backend, size, size);
    if (!canvas || !canvas->initializeContext()) return false;
    canvas->beginFrame();
    scene.draw(*canvas);
    canvas->endFrame();
    return canvas->readPixelsRGBA(pixels);
}

int reportUnavailableGL(bool required, const char *reason)
{
    std::cerr << "PARITY_PROBE " << (required ? "FAIL" : "SKIP")
              << " gl_context reason=" << reason << "\n";
    return required ? 1 : 0;
}

} // anonymous namespace

int main(int argc, char **argv)
{
    Args args;
    if (!parseArgs(argc, argv, args)) return 1;

    std::filesystem::create_directories(args.outDir);

    // ---- GL context (hidden) ----
    if (!glfwInit()) return reportUnavailableGL(args.requireGL, "glfw_init_failed");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    GLFWwindow *window = glfwCreateWindow(args.size, args.size,
                                          "parity_probe", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return reportUnavailableGL(args.requireGL, "gl33_context_unavailable");
    }
    glfwMakeContextCurrent(window);
    if (!wsc::Canvas::loadOpenGL(
            reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return reportUnavailableGL(args.requireGL, "loadOpenGL_failed");
    }

    const auto scenes = buildScenes();

    // Header line makes the output easy to consume from a script.
    std::cout << "PARITY_PROBE_HEADER"
              << " size=" << args.size
              << " out=" << args.outDir
              << " scenes=" << scenes.size() << "\n";

    int failed = 0;
    int rendered = 0;

    // Top divergences summary at the end.
    struct RankRow { std::string id; DiffStats s; };
    std::vector<RankRow> ranking;

    for (const Scene &scene : scenes) {
        if (!args.only.empty() && scene.id != args.only) continue;

        std::vector<unsigned char> softPixels, glPixels;
        const bool okSoft = renderScene(wsc::Canvas::Backend::Software, args.size,
                                        scene, softPixels);
        const bool okGL = renderScene(wsc::Canvas::Backend::OpenGL, args.size,
                                      scene, glPixels);
        if (!okSoft || !okGL) {
            std::cout << "PARITY_PROBE_SCENE id=" << scene.id
                      << " status=RENDER_FAIL"
                      << " software=" << (okSoft ? "ok" : "fail")
                      << " opengl=" << (okGL ? "ok" : "fail") << "\n";
            ++failed;
            continue;
        }

        const DiffStats s = diffImages(softPixels, glPixels, args.size, args.size);

        std::cout << "PARITY_PROBE_SCENE id=" << scene.id
                  << " w=" << args.size << " h=" << args.size
                  << " max=" << s.maxDelta
                  << " mean=" << std::fixed << std::setprecision(3) << s.meanDelta
                  << " bad4=" << std::fixed << std::setprecision(4) << s.badPixel4
                  << " bad16=" << std::fixed << std::setprecision(4) << s.badPixel16
                  << " bad32=" << std::fixed << std::setprecision(4) << s.badPixel32
                  << " worst=" << s.worstX << "," << s.worstY
                  << ",ch" << s.worstChannel
                  << " notes=\"" << scene.notes << "\""
                  << "\n";

        writePPM(args.outDir + "/" + scene.id + ".software.ppm",
                 softPixels, args.size, args.size);
        writePPM(args.outDir + "/" + scene.id + ".opengl.ppm",
                 glPixels, args.size, args.size);
        writeDiffPPM(args.outDir + "/" + scene.id + ".diff.ppm",
                     softPixels, glPixels, args.size, args.size);

        ranking.push_back({scene.id, s});
        ++rendered;
    }

    // Rank by bad16 (moderate-difference ratio) descending.
    std::sort(ranking.begin(), ranking.end(), [](const RankRow &a, const RankRow &b){
        if (a.s.badPixel16 != b.s.badPixel16) return a.s.badPixel16 > b.s.badPixel16;
        if (a.s.maxDelta != b.s.maxDelta) return a.s.maxDelta > b.s.maxDelta;
        return a.s.meanDelta > b.s.meanDelta;
    });

    std::cout << "PARITY_PROBE_RANK_BEGIN by=bad16\n";
    const int topN = std::min<int>(10, static_cast<int>(ranking.size()));
    for (int i = 0; i < topN; ++i) {
        const auto &r = ranking[i];
        std::cout << "  rank=" << (i + 1)
                  << " id=" << r.id
                  << " max=" << r.s.maxDelta
                  << " mean=" << std::fixed << std::setprecision(3) << r.s.meanDelta
                  << " bad4=" << std::fixed << std::setprecision(4) << r.s.badPixel4
                  << " bad16=" << std::fixed << std::setprecision(4) << r.s.badPixel16
                  << " bad32=" << std::fixed << std::setprecision(4) << r.s.badPixel32
                  << "\n";
    }
    std::cout << "PARITY_PROBE_RANK_END\n";

    std::cout << "PARITY_PROBE_SUMMARY rendered=" << rendered
              << " render_failed=" << failed
              << "\n";

    glfwDestroyWindow(window);
    glfwTerminate();

    // Exit 0 even when there are large deltas — this is a probe, not a gate.
    // A non-zero code only signals infrastructure problems.
    return failed == 0 ? 0 : 2;
}
