#include "support/HiddenGLContext.h"
// Exercise each native text entry point after lazy initialization,
// including measurement, fallback configuration and rendering.
#include <wsc/Canvas.h>
#include <wsc/Paint.h>
#include <wsc/Font.h>
#include <wsc/FontSystem.h>
#include <iostream>
#include <stdexcept>
#include <glad/glad.h>
#include <wsc/Image.h>



static void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}

int main() {
    try {
        wsc::Paint paint;
        paint.setTextSize(24);
        paint.setColor(wsc::Color::WHITE);
        for (int firstApi = 0; firstApi < 6; ++firstApi) {
            auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 96);
            require(canvas != nullptr, "create canvas");
            switch (firstApi) {
            case 0: require(canvas->measureText("SampleText", paint) > 0, "lazy width"); break;
            case 1: require(canvas->measureTextBounds("SampleText", paint).getWidth() > 0, "lazy bounds"); break;
            case 2: require(canvas->measureTextMetrics("SampleText", paint).height > 0, "lazy metrics"); break;
            case 3: {
                const auto faces = wsc::FontSystem::defaultSystemFontFaces();
                require(!faces.empty(), "installed default fonts");
                require(canvas->setFontFallbackChain(wsc::FontFallbackChain(faces.front().family())), "lazy fallback");
                break;
            }
            case 4: require(canvas->setTextBackend(wsc::Canvas::TextBackend::Portable), "explicit backend"); break;
            case 5: break; // drawText itself must initialize the backend.
            }
            canvas->beginFrame();
            canvas->drawText("SampleText", 8, 40, paint);
            canvas->endFrame();
            auto pixels = canvas->readPixelsRGBA();
            int covered = 0;
            for (size_t i = 3; i < pixels.size(); i += 4) covered += pixels[i] != 0;
            require(covered > 20, "native text must render after lazy initialization");
        }
        std::cout << "PASS: 6 lazy/native text entry paths render nonempty pixels\n";
        HiddenGLContext context;
        {
            auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 256, 96);
            require(canvas && canvas->initializeContext(), "GL canvas");
            wsc::Image image;
            std::vector<unsigned char> rgba(8 * 8 * 4, 255);
            require(canvas->loadImageFromRGBA(image, rgba, 8, 8), "image creation");
            glEnable(0xffffffffu); // Deliberate unrelated GL_INVALID_ENUM.
            rgba[0] = 0;
            require(canvas->updateImageRGBA(image, rgba, 0, 0, 8, 8, false), "stale GL error must not reject valid upload");
            require(!canvas->updateImageRGBA(image, rgba, 7, 7, 8, 8, false), "invalid upload must still fail");
            // Full-canvas clipping forces the regular path, providing a
            // same-GPU image reference for the new instanced atlas path.
            std::vector<unsigned char> reference;
            for (bool clip : {true, false}) {
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                canvas->beginFrame();
                canvas->save();
                if (clip) canvas->clipRect(wsc::RectF(0, 0, 256, 96));
                for (int i = 0; i < 5; ++i)
                    canvas->drawImage(image, wsc::RectF(0, 0, 8, 8), wsc::RectF(8 + i * 30, 8, 20, 20), paint);
                canvas->restore();
                canvas->endFrame();
                glFinish();
                auto pixels = canvas->readPixelsRGBA();
                int covered = 0;
                for (size_t i = 0; i < pixels.size(); i += 4)
                    covered += pixels[i] != 0 || pixels[i + 1] != 0 || pixels[i + 2] != 0;
                require(covered >= 2000 && covered < 2500, "five visible image quads");
                if (clip) reference = std::move(pixels);
                else require(!pixels.empty() && pixels == reference, "instanced image differs from clipped reference");
            }
        }

        std::cout << "PASS: stale GL error, invalid upload and instanced image reference\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
