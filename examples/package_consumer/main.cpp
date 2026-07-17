#include <wsc/wsc.h>

#include <cmath>
#include <filesystem>
#include <iostream>

namespace {

const char *packageTargetName()
{
#if defined(WHATSCANVAS_EXAMPLE_PACKAGE_OPENGL)
    return "WhatsCanvas::OpenGL";
#elif defined(WHATSCANVAS_EXAMPLE_PACKAGE_OPENGLES)
    return "WhatsCanvas::OpenGLES";
#else
    return "WhatsCanvas::Software";
#endif
}

const char *requestedBackendName()
{
#if defined(WHATSCANVAS_EXAMPLE_RUNTIME_OPENGL)
    return "OpenGL";
#elif defined(WHATSCANVAS_EXAMPLE_RUNTIME_OPENGLES)
    return "OpenGLES";
#elif defined(WHATSCANVAS_EXAMPLE_RUNTIME_VULKAN)
    return "Vulkan";
#else
    return "Software";
#endif
}

wsc::Canvas::Backend requestedBackend()
{
#if defined(WHATSCANVAS_EXAMPLE_RUNTIME_OPENGL)
    return wsc::Canvas::Backend::OpenGL;
#elif defined(WHATSCANVAS_EXAMPLE_RUNTIME_OPENGLES)
    return wsc::Canvas::Backend::OpenGLES;
#elif defined(WHATSCANVAS_EXAMPLE_RUNTIME_VULKAN)
    return wsc::Canvas::Backend::Vulkan;
#else
    return wsc::Canvas::Backend::Software;
#endif
}

const char *backendName(wsc::Canvas::Backend backend)
{
    switch (backend) {
    case wsc::Canvas::Backend::OpenGL:
        return "OpenGL";
    case wsc::Canvas::Backend::OpenGLES:
        return "OpenGLES";
    case wsc::Canvas::Backend::Vulkan:
        return "Vulkan";
    case wsc::Canvas::Backend::Software:
        return "Software";
    default:
        return "Unknown";
    }
}

} // namespace

int main(int argc, char **argv)
{
    (void)argc;
    std::cout << "Linked package target: " << packageTargetName() << "\n";
    std::cout << "Requested runtime backend: " << requestedBackendName() << "\n";

#if defined(WHATSCANVAS_EXAMPLE_RUNTIME_OPENGL)
    std::cout
        << "Create your GL context first, call wsc::Canvas::loadOpenGL(...), "
        << "then create Backend::OpenGL. This consumer example focuses on "
        << "package linkage, so it does not create a window/context for you.\n";
    return 0;
#elif defined(WHATSCANVAS_EXAMPLE_RUNTIME_OPENGLES)
    std::cout
        << "Create your GLES context first, call wsc::Canvas::loadOpenGL(...), "
        << "then create Backend::OpenGLES. This consumer example focuses on "
        << "package linkage, so it does not create a window/context for you.\n";
    return 0;
#else
    constexpr float width = 800.0f;
    constexpr float height = 600.0f;
    auto canvas = wsc::Canvas::create(requestedBackend(), static_cast<int>(width), static_cast<int>(height));
    if (!canvas) {
        std::cerr << "Canvas::create returned nullptr for backend " << requestedBackendName() << "\n";
        return 1;
    }

    canvas->setSize(static_cast<int>(width), static_cast<int>(height));
    canvas->beginFrame();
    canvas->drawColor(wsc::Color(24, 26, 34));

    wsc::Paint background;
    background.setAntiAlias(true);
    background.setLinearGradient(0.0f, 0.0f, width, height,
                                 wsc::Color(40, 44, 60), wsc::Color(18, 20, 28));
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, width, height), background);

    constexpr float t = 0.8f;
    const float cx = width * 0.5f + std::cos(t) * 180.0f;
    const float cy = height * 0.5f + std::sin(t * 1.3f) * 120.0f;

    wsc::Paint circle;
    circle.setAntiAlias(true);
    circle.setRadialGradient(cx, cy, 90.0f,
                             wsc::Color(120, 200, 255), wsc::Color(40, 90, 160));
    circle.setShadowLayer(24.0f, 0.0f, 10.0f, wsc::Color(0, 0, 0, 120));
    canvas->drawCircle(cx, cy, 90.0f, circle);

    canvas->save();
    canvas->translate(width * 0.5f, height * 0.5f);
    canvas->rotate(t * 0.7f);
    wsc::Paint box;
    box.setAntiAlias(true);
    box.setColor(wsc::Color(255, 180, 80));
    canvas->drawRoundRect(wsc::RectF(-60.0f, -40.0f, 120.0f, 80.0f), 18.0f, box);
    canvas->restore();

    wsc::Paint label;
    label.setColor(wsc::Color::WHITE);
    label.setTextSize(26.0f);
    label.setFontFamily("Inter");
    canvas->drawText("WhatsCanvas - Consumer", 24.0f, 40.0f, label);
    canvas->endFrame();

    const std::filesystem::path executablePath = std::filesystem::path(argv[0]).lexically_normal();
    const std::filesystem::path outputPath = executablePath.parent_path() / "package_consumer_output.ppm";
    if (!canvas->savePixelsPPM(outputPath.string())) {
        std::cerr << "Failed to save image to " << outputPath.string() << "\n";
        return 2;
    }

    std::cout << "Created backend: " << backendName(canvas->backend()) << "\n";
    std::cout << "Canvas size: " << canvas->getWidth() << "x" << canvas->getHeight() << "\n";
    std::cout << "Wrote image: " << outputPath.string() << "\n";
    return 0;
#endif
}