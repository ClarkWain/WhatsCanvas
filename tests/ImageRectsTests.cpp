#include "support/HiddenGLContext.h"
#include <wsc/CanvasStats.h>
// Public bulk image API must match ordered scalar drawing, including fallback.
#include <glad/glad.h>
#include <wsc/Canvas.h>
#include <wsc/Image.h>
#include <wsc/Paint.h>
#include <wsc/Path.h>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

int main() {
    try {
        HiddenGLContext context;
        wsc::Canvas::setGammaCorrect(false);
        for (auto backend : {wsc::Canvas::Backend::OpenGL, wsc::Canvas::Backend::Software}) {
            for (int mode = 0; mode < 8; ++mode) {
                std::vector<std::vector<unsigned char>> reference;
                for (bool bulk : {false, true}) {
                    auto canvas = wsc::Canvas::create(backend, 256, 96);
                    require(canvas && canvas->initializeContext(), "canvas");
                    wsc::Image image;
                    std::vector<unsigned char> pixels(16 * 16 * 4);
                    for (int i = 0; i < 256; ++i) {
                        pixels[i*4] = i; pixels[i*4+1] = (i * 3) % 256;
                        pixels[i*4+2] = (i * 7) % 256; pixels[i*4+3] = 80 + i % 176;
                    }
                    require(canvas->loadImageFromRGBA(image, pixels, 16, 16), "image");
                    for (int repeat = 0; repeat < 3; ++repeat) {
                    std::vector<wsc::Canvas::ImageRect> rects = {
                        {{0, 0, 16, 16}, {4, 4, 40, 40}},
                        {{-4, -3, 12, 13}, {20, 20, 40, 50}},
                        {{2, 3, 12, 11}, {80, 30, -28, -22}},
                        {{20, 20, 8, 8}, {100, 5, 20, 30}},
                        {{0, 0, 0, 16}, {120, 5, 20, 30}},
                        {{0, 0, 16, 16}, {32, 18, 37, 39}}};
                    wsc::Paint paint;
                    paint.setColor(wsc::Color(.713f, .329f, .867f, .931f));
                    paint.setAlpha(.4137f + repeat * .1f);
                    if (mode == 3) paint.setImageSampling(wsc::Paint::ImageSampling::NEAREST);
                    if (mode == 4) paint.setColorMatrix({1,0,0,0,.07f, 0,.8f,0,0,.03f, 0,0,.6f,0,0, 0,0,0,.9f,0});
                    glViewport(0, 0, 256, 96); glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
                    canvas->beginFrame(); canvas->save();
                    if (mode == 1) canvas->clipRect(wsc::RectF(12, 10, 53, 40));
                    if (mode == 2) { wsc::Path p; p.moveTo(5,5); p.lineTo(100,15); p.lineTo(50,75); p.close(); canvas->clipPath(p); }
                    if (mode == 5) { canvas->translate(110, 4); canvas->scale(-1, 1); canvas->rotate(.07f); }
                    if (mode == 6) { wsc::Paint layer; layer.setAlpha(.7231f); canvas->saveLayer(wsc::RectF(0, 0, 256, 96), layer); }
                    const auto draw = [&](wsc::Canvas &c) {
                        if (bulk) {
                            c.drawImageRects(image, nullptr, 0, paint);
                            c.drawImageRects(image, rects.data(), 3, paint);
                            c.drawImageRects(image, rects.data() + 3, rects.size() - 3, paint);
                        }
                        else for (const auto &r : rects) c.drawImage(image, r.source, r.destination, paint);
                    };
                    if (mode == 7) {
                        auto picture = canvas->recordPicture(draw);
                        require(bool(picture), "record picture"); canvas->drawPicture(*picture);
                    } else draw(*canvas);
                    if (mode == 6) canvas->restore();
                    canvas->restore(); canvas->endFrame(); glFinish();
                    auto output = canvas->readPixelsRGBA();
                    std::size_t covered = 0;
                    for (std::size_t i = 3; i < output.size(); i += 4) covered += output[i] != 0;
                    require(covered > 100, "nonempty output");
                    if (!bulk) reference.push_back(std::move(output));
                    else if (output != reference.at(repeat)) {
                        std::cerr << "backend=" << int(backend) << " mode=" << mode << '\n';
                        throw std::runtime_error("bulk/scalar pixels differ");
                    }
                    }
                }
            }
        }

        {
            auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 256, 96);
            require(canvas && canvas->initializeContext(), "clip cache canvas");
            wsc::Image image;
            require(canvas->loadImageFromRGBA(image, std::vector<unsigned char>(16 * 16 * 4, 255), 16, 16), "clip image");
            wsc::Paint paint;
            paint.setColor(wsc::Color::WHITE);
            for (int frame = 0; frame < 160; ++frame) {
                canvas->beginFrame(); canvas->save();
                // Two translations share local geometry; subsequent shapes
                // and scale changes exercise misses and bounded eviction.
                const float d = frame < 2 ? 0.f : frame * .013f;
                canvas->translate(frame % 7, 0);
                if (frame == 2) canvas->scale(1.25f, 1.25f);
                wsc::Path path;
                path.moveTo(10, 10); path.lineTo(80 + d, 15);
                path.lineTo(30, 70); path.close();
                canvas->clipPath(path);
                canvas->drawImage(image, wsc::RectF(0, 0, 128, 96), paint);
                canvas->restore(); canvas->endFrame();
                auto stats = canvas->getRenderStats();
                require(stats.aaCacheSize <= 128 && stats.aaCacheBytes <= 8u * 1024u * 1024u, "clip AA cache bounds");
                if (frame == 1) require(stats.aaCacheHits > 0, "translated clip must reuse local AA mesh");
                if (frame == 2) require(stats.aaCacheMisses >= 2, "changed scale must rebuild AA mesh");
            }
            canvas->releaseResources();
            require(canvas->getRenderStats().aaCacheSize == 0, "clip AA cache must release with canvas resources");
        }
        std::cout << "PASS: 48 GL/software bulk/scalar pixel comparisons across reused frames, fallback and Picture recording\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
