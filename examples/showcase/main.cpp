#include <iostream>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <array>
#include <limits>
#include <vector>
#include <GLFW/glfw3.h>
#include "wsc/wsc.h"

using namespace wsc;

const float PI = 3.14159265359f;
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;
constexpr unsigned int kOpenGLMultisample = 0x809D;

std::string getEnvironmentValue(const char* name)
{
#ifdef _MSC_VER
    char* value = nullptr;
    size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
        return std::string();
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

bool parseUint64(const std::string& text, std::uint64_t& value)
{
    if (text.empty()) {
        return false;
    }

    std::uint64_t result = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }

    value = result;
    return true;
}

bool parseFloat(const std::string& text, float& value)
{
    if (text.empty()) {
        return false;
    }

    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }

    value = parsed;
    return true;
}

std::vector<unsigned char> makeValidationTexture(int width, int height, int variant)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4U);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool checker = ((x / 8) + (y / 8)) % 2 == 0;
            const float fx = static_cast<float>(x) / static_cast<float>(width - 1);
            const float fy = static_cast<float>(y) / static_cast<float>(height - 1);
            const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4U;
            if (variant == 0) {
                pixels[offset + 0] = static_cast<unsigned char>(checker ? 255 : 30 + fx * 150.0f);
                pixels[offset + 1] = static_cast<unsigned char>(40 + fy * 190.0f);
                pixels[offset + 2] = static_cast<unsigned char>(checker ? 90 + fy * 120.0f : 240);
                pixels[offset + 3] = static_cast<unsigned char>(220 + checker * 35);
            } else {
                const bool stripe = ((x + y) / 6) % 3 == 0;
                pixels[offset + 0] = static_cast<unsigned char>(30 + fx * 210.0f);
                pixels[offset + 1] = static_cast<unsigned char>(stripe ? 245 : 80 + fy * 120.0f);
                pixels[offset + 2] = static_cast<unsigned char>(170 + (1.0f - fx) * 70.0f);
                pixels[offset + 3] = static_cast<unsigned char>(stripe ? 190 : 245);
            }
        }
    }
    return pixels;
}

std::string utf8ValidationText()
{
    return std::string("UTF-8 text validation: Latin, ") +
        "\xE4\xB8\xAD\xE6\x96\x87, " +
        "\xE6\xB7\xB7\xE6\x8E\x92 layout, emoji " +
        "\xF0\x9F\x9A\x80 \xF0\x9F\x8C\x88, wrap and ellipsis behavior.";
}

struct ValidationImages
{
    Image checker;
    Image bands;
    bool checkerLoaded = false;
    bool bandsLoaded = false;
};

void drawTextHeavyValidationScene(Canvas& canvas, float currentTime)
{
    Paint backdrop;
    backdrop.setStyle(Paint::Style::FILL);
    backdrop.setLinearGradient(0.0f, 0.0f, 800.0f, 600.0f,
                               {
                                   Paint::ColorStop(0.0f, Color(10, 18, 30)),
                                   Paint::ColorStop(0.55f, Color(28, 34, 44)),
                                   Paint::ColorStop(1.0f, Color(40, 36, 24))
                               });
    canvas.drawRect(RectF(0.0f, 0.0f, 800.0f, 600.0f), backdrop);

    Paint card;
    card.setStyle(Paint::Style::FILL_AND_STROKE);
    card.setFillColor(Color(255, 255, 255, 18));
    card.setStrokeColor(Color(255, 255, 255, 42));
    card.setStrokeWidth(1.0f);

    Paint title;
    title.setStyle(Paint::Style::FILL);
    title.setColor(Color(250, 250, 245, 235));
    title.setTextSize(24.0f);
    title.setFontFamily("Segoe UI");

    Paint body;
    body.setStyle(Paint::Style::FILL);
    body.setColor(Color(226, 232, 240, 225));
    body.setTextSize(15.0f);
    body.setFontFamily("Segoe UI");

    Paint small;
    small.setStyle(Paint::Style::FILL);
    small.setColor(Color(150, 210, 255, 215));
    small.setTextSize(11.0f);
    small.setFontFamily("Consolas");
    small.setLetterSpacing(0.6f);

    canvas.drawText("Text Validation", 32.0f, 36.0f, title);
    const std::string sample = utf8ValidationText();
    for (int row = 0; row < 6; ++row) {
        const float y = 72.0f + row * 82.0f;
        canvas.drawRoundRect(RectF(28.0f, y, 744.0f, 64.0f), 6.0f, card);
        Paint rowPaint = body;
        rowPaint.setTextSize(12.0f + static_cast<float>(row) * 1.6f);
        rowPaint.setLetterSpacing((row % 3) * 0.55f);
        if (row % 2 == 1) {
            rowPaint.setTextAlign(Paint::TextAlign::CENTER);
            canvas.drawTextBox(sample, RectF(52.0f, y + 9.0f, 696.0f, 42.0f), 18.0f, 2, true, rowPaint);
        } else {
            canvas.drawTextBox(sample, RectF(52.0f, y + 9.0f, 696.0f, 42.0f), 18.0f, 2, true, rowPaint);
        }
        const Canvas::TextMetrics metrics = canvas.measureTextMetrics(sample, rowPaint);
        canvas.drawText("w=" + std::to_string(static_cast<int>(metrics.width)) +
                        " h=" + std::to_string(static_cast<int>(metrics.height)),
                        628.0f, y + 54.0f, small);
    }

    Path wave;
    wave.moveTo(70.0f, 545.0f);
    wave.cubicTo(210.0f, 486.0f, 310.0f, 592.0f, 448.0f, 528.0f);
    wave.cubicTo(552.0f, 480.0f, 662.0f, 548.0f, 742.0f, 510.0f);

    Paint wavePaint;
    wavePaint.setStyle(Paint::Style::STROKE);
    wavePaint.setStrokeColor(Color(120, 220, 255, 170));
    wavePaint.setStrokeWidth(3.0f);
    wavePaint.setStrokeCap(Paint::StrokeCap::ROUND);
    canvas.drawPath(wave, wavePaint);

    Paint pathText;
    pathText.setStyle(Paint::Style::FILL);
    pathText.setColor(Color(255, 225, 130, 225));
    pathText.setTextSize(13.0f);
    pathText.setFontFamily("Georgia");
    pathText.setLetterSpacing(1.0f);
    canvas.drawTextOnPath("path text metrics transform clip", wave, std::fmod(currentTime * 24.0f, 80.0f), -12.0f, pathText);
}

void drawImageHeavyValidationScene(Canvas& canvas, const ValidationImages& images, float currentTime)
{
    Paint background;
    background.setStyle(Paint::Style::FILL);
    background.setRadialGradient(400.0f, 300.0f, 520.0f,
                                 {
                                     Paint::ColorStop(0.0f, Color(28, 42, 44)),
                                     Paint::ColorStop(0.62f, Color(18, 24, 32)),
                                     Paint::ColorStop(1.0f, Color(8, 10, 16))
                                 });
    canvas.drawRect(RectF(0.0f, 0.0f, 800.0f, 600.0f), background);

    if (!images.checkerLoaded || !images.bandsLoaded) {
        Paint errorPaint;
        errorPaint.setStyle(Paint::Style::FILL);
        errorPaint.setColor(Color(255, 120, 120, 235));
        errorPaint.setTextSize(18.0f);
        errorPaint.setFontFamily("Consolas");
        canvas.drawText("validation images failed to load", 40.0f, 48.0f, errorPaint);
        return;
    }

    Paint base;
    base.setColor(Color(255, 255, 255, 230));
    base.setImageSampling(Paint::ImageSampling::NEAREST);

    Paint filtered;
    filtered.setColor(Color(255, 255, 255, 230));
    filtered.setImageSampling(Paint::ImageSampling::MIPMAP_LINEAR);
    filtered.setColorMatrix(std::array<float, 20>{
        1.15f, 0.0f,  0.0f, 0.0f, 0.0f,
        0.0f,  1.0f,  0.0f, 0.0f, 0.0f,
        0.0f,  0.0f,  0.9f, 0.0f, 0.0f,
        0.0f,  0.0f,  0.0f, 1.0f, 0.0f
    });

    Paint tiled = base;
    tiled.setImageTileMode(Paint::ImageTileMode::MIRROR);

    Paint decal = base;
    decal.setImageTileMode(Paint::ImageTileMode::DECAL);
    decal.setAlpha(0.72f);

    canvas.drawImage(images.checker, RectF(36.0f, 40.0f, 160.0f, 120.0f), base);
    canvas.drawImage(images.checker, RectF(8.0f, 8.0f, 40.0f, 40.0f), RectF(220.0f, 40.0f, 160.0f, 120.0f), filtered);
    canvas.drawImageFit(images.bands, RectF(404.0f, 40.0f, 160.0f, 120.0f), Canvas::ImageFit::CONTAIN, Canvas::ImageAnchor::CENTER, base);
    canvas.drawImageFit(images.bands, RectF(588.0f, 40.0f, 160.0f, 120.0f), Canvas::ImageFit::COVER, 0.2f, 0.8f, filtered);

    canvas.drawImageNinePatch(images.checker, RectF(18.0f, 18.0f, 28.0f, 28.0f), RectF(36.0f, 192.0f, 248.0f, 92.0f), base);
    canvas.drawImageTiled(images.bands, RectF(312.0f, 192.0f, 204.0f, 92.0f), 32.0f, 32.0f, tiled);
    canvas.drawImageTiled(images.checker, RectF(544.0f, 192.0f, 204.0f, 92.0f), 28.0f, 28.0f, decal);

    Paint border;
    border.setStyle(Paint::Style::STROKE);
    border.setStrokeColor(Color(255, 255, 255, 68));
    border.setStrokeWidth(1.0f);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 6; ++x) {
            const float px = 44.0f + x * 118.0f;
            const float py = 328.0f + y * 72.0f;
            canvas.save();
            canvas.translate(px + 48.0f, py + 26.0f);
            canvas.rotate((currentTime * 0.18f) + static_cast<float>(x + y) * 0.14f);
            canvas.drawImage((x + y) % 2 == 0 ? images.checker : images.bands,
                             RectF(-44.0f, -24.0f, 88.0f, 48.0f),
                             (x % 3 == 0) ? filtered : base);
            canvas.restore();
            canvas.drawRect(RectF(px, py, 96.0f, 52.0f), border);
        }
    }
}

// Callback: update the viewport when the framebuffer size changes
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    Canvas* canvas = static_cast<Canvas*>(glfwGetWindowUserPointer(window));
    if (canvas && width > 0 && height > 0) {
        canvas->setSize(width, height);
    }
}

int main() {
    std::cout << "Starting application..." << std::endl;
    const bool disableMsaa = !getEnvironmentValue("WHATSCANVAS_DISABLE_MSAA").empty();
    const bool exerciseClipPath = !getEnvironmentValue("WHATSCANVAS_EXERCISE_CLIP_PATH").empty();
    const std::string validationScene = getEnvironmentValue("WHATSCANVAS_VALIDATION_SCENE");
    const bool runTextValidation = validationScene == "text-heavy";
    const bool runImageValidation = validationScene == "image-heavy";
    if (!validationScene.empty() && !runTextValidation && !runImageValidation) {
        std::cerr << "Unknown validation scene: " << validationScene << std::endl;
    }

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    std::cout << "GLFW initialized successfully." << std::endl;

    // Configure the GLFW context version and OpenGL settings
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // major version
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); // minor version
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // use the core profile
    glfwWindowHint(GLFW_SAMPLES, disableMsaa ? 0 : 4); // request MSAA; GLFW falls back if it is unavailable.
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    // macOS requires a forward-compatible context
    #ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    // Create the window
    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight, "WhatsCanvas Demo", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "GLFW window created successfully." << std::endl;

    // Make the context current
    glfwMakeContextCurrent(window);
    std::cout << "GLFW context set successfully." << std::endl;

    if (!Canvas::loadOpenGL(reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        std::cerr << "Failed to load OpenGL functions" << std::endl;
        return -1;
    }
    std::cout << "OpenGL functions loaded successfully." << std::endl;

    // Check the OpenGL version
    std::cout << "OpenGL " << Canvas::getOpenGLVersionString() << " loaded." << std::endl;

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0) {
        framebufferWidth = kWindowWidth;
    }
    if (framebufferHeight <= 0) {
        framebufferHeight = kWindowHeight;
    }

    // Set the viewport
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    if (!disableMsaa) {
        glEnable(kOpenGLMultisample);
    }

    // Set the clear color
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    Canvas canvas;
        canvas.setSize(framebufferWidth, framebufferHeight);
        glfwSetWindowUserPointer(window, &canvas);
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    
    Paint paint1;
    paint1.setStrokeWidth(26.0f);
    paint1.setStyle(Paint::Style::STROKE);
    paint1.setStrokeCap(Paint::StrokeCap::ROUND);
    paint1.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint pathPaint;
    pathPaint.setFillColor(Color(40, 140, 240));
    pathPaint.setStrokeColor(Color::WHITE);
    pathPaint.setStrokeWidth(18.0f);
    pathPaint.setStyle(Paint::Style::FILL_AND_STROKE);
    pathPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    pathPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint concavePaint;
    concavePaint.setFillColor(Color(180, 95, 245, 210));
    concavePaint.setStrokeColor(Color::WHITE);
    concavePaint.setStrokeWidth(5.0f);
    concavePaint.setStyle(Paint::Style::FILL_AND_STROKE);
    concavePaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint evenOddPaint;
    evenOddPaint.setFillColor(Color(40, 235, 190, 220));
    evenOddPaint.setStrokeColor(Color::WHITE);
    evenOddPaint.setStrokeWidth(4.0f);
    evenOddPaint.setStyle(Paint::Style::FILL_AND_STROKE);

    Paint saveLayerPaint;
    saveLayerPaint.setAlpha(0.82f);

    Paint layerCirclePaint;
    layerCirclePaint.setStyle(Paint::Style::FILL);
    layerCirclePaint.setFillColor(Color(255, 95, 70, 210));

    Paint layerRectPaint;
    layerRectPaint.setStyle(Paint::Style::FILL);
    layerRectPaint.setFillColor(Color(80, 220, 255, 190));
    layerRectPaint.setBlendMode(Paint::BlendMode::ADD);

    Paint porterDuffBasePaint;
    porterDuffBasePaint.setStyle(Paint::Style::FILL);
    porterDuffBasePaint.setFillColor(Color(140, 105, 255, 210));

    Paint porterDuffSrcInPaint;
    porterDuffSrcInPaint.setStyle(Paint::Style::FILL);
    porterDuffSrcInPaint.setFillColor(Color(255, 230, 80, 235));
    porterDuffSrcInPaint.setBlendMode(Paint::BlendMode::SRC_IN);

    Paint porterDuffDstOutPaint;
    porterDuffDstOutPaint.setStyle(Paint::Style::FILL);
    porterDuffDstOutPaint.setFillColor(Color(255, 255, 255, 230));
    porterDuffDstOutPaint.setBlendMode(Paint::BlendMode::DST_OUT);

    Paint roundStrokePaint;
    roundStrokePaint.setStrokeColor(Color(255, 190, 70));
    roundStrokePaint.setStrokeWidth(34.0f);
    roundStrokePaint.setStyle(Paint::Style::STROKE);
    roundStrokePaint.setStrokeCap(Paint::StrokeCap::ROUND);
    roundStrokePaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint curvePaint;
    curvePaint.setStrokeColor(Color(220, 120, 255));
    curvePaint.setStrokeWidth(9.0f);
    curvePaint.setStyle(Paint::Style::STROKE);
    curvePaint.setStrokeCap(Paint::StrokeCap::ROUND);
    curvePaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint rectPaint;
    rectPaint.setFillColor(Color(40, 180, 120));
    rectPaint.setStrokeColor(Color::WHITE);
    rectPaint.setStrokeWidth(8.0f);
    rectPaint.setStyle(Paint::Style::FILL_AND_STROKE);

    Paint roundedCornerPathPaint;
    roundedCornerPathPaint.setStyle(Paint::Style::STROKE);
    roundedCornerPathPaint.setStrokeColor(Color(255, 245, 120, 240));
    roundedCornerPathPaint.setStrokeWidth(5.0f);
    roundedCornerPathPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    roundedCornerPathPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);
    roundedCornerPathPaint.setCornerPathEffect(24.0f);

    Paint transformHitPaint;
    transformHitPaint.setStyle(Paint::Style::STROKE);
    transformHitPaint.setStrokeWidth(10.0f);

    Paint gradientRectPaint;
    gradientRectPaint.setStyle(Paint::Style::FILL_AND_STROKE);
    gradientRectPaint.setStrokeColor(Color::WHITE);
    gradientRectPaint.setStrokeWidth(8.0f);
    gradientRectPaint.setLinearGradient(500.0f, 300.0f, 610.0f, 370.0f,
                                        {
                                            Paint::ColorStop(0.0f, Color(255, 210, 60, 230)),
                                            Paint::ColorStop(0.48f, Color(80, 235, 165, 235)),
                                            Paint::ColorStop(1.0f, Color(55, 185, 255, 230))
                                        });
    gradientRectPaint.setShaderTileMode(Paint::ShaderTileMode::MIRROR);
    gradientRectPaint.setShadowLayer(18.0f, 12.0f, 12.0f, Color(0, 0, 0, 115));

    Paint roundRectPaint;
    roundRectPaint.setStyle(Paint::Style::STROKE);
    roundRectPaint.setStrokeColor(Color(250, 90, 90));
    roundRectPaint.setStrokeWidth(12.0f);
    roundRectPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint asymmetricRoundRectPaint;
    asymmetricRoundRectPaint.setStyle(Paint::Style::FILL_AND_STROKE);
    asymmetricRoundRectPaint.setFillColor(Color(120, 130, 255, 180));
    asymmetricRoundRectPaint.setStrokeColor(Color(255, 255, 255, 220));
    asymmetricRoundRectPaint.setStrokeWidth(4.0f);
    asymmetricRoundRectPaint.setAlpha(0.78f);
    asymmetricRoundRectPaint.setBlendMode(Paint::BlendMode::SCREEN);

    Paint circlePaint;
    circlePaint.setFillColor(Color(255, 140, 70));
    circlePaint.setStrokeColor(Color::WHITE);
    circlePaint.setStrokeWidth(10.0f);
    circlePaint.setStyle(Paint::Style::FILL_AND_STROKE);
    circlePaint.setRadialGradient(610.0f, 505.0f, 62.0f,
                                  {
                                      Paint::ColorStop(0.0f, Color(255, 245, 170, 245)),
                                      Paint::ColorStop(0.45f, Color(255, 135, 90, 238)),
                                      Paint::ColorStop(1.0f, Color(150, 55, 210, 225))
                                  });
    circlePaint.setShaderTileMode(Paint::ShaderTileMode::CLAMP);

    Paint ovalPaint;
    ovalPaint.setStyle(Paint::Style::STROKE);
    ovalPaint.setStrokeColor(Color(70, 210, 255));
    ovalPaint.setStrokeWidth(10.0f);
    ovalPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint arcPaint;
    arcPaint.setStyle(Paint::Style::STROKE);
    arcPaint.setStrokeColor(Color(255, 110, 210));
    arcPaint.setStrokeWidth(7.0f);
    arcPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    arcPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint dashedPaint;
    dashedPaint.setStyle(Paint::Style::STROKE);
    dashedPaint.setStrokeColor(Color(255, 245, 120, 230));
    dashedPaint.setStrokeWidth(6.0f);
    dashedPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    dashedPaint.setDashPathEffect(std::vector<float>{18.0f, 10.0f, 6.0f, 10.0f}, -12.0f);

    Paint closedDashPaint;
    closedDashPaint.setStyle(Paint::Style::STROKE);
    closedDashPaint.setStrokeColor(Color(120, 255, 210, 220));
    closedDashPaint.setStrokeWidth(3.0f);
    closedDashPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    closedDashPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);
    closedDashPaint.setDashPathEffect(std::vector<float>{22.0f, 12.0f}, -9.0f);

    Paint clipBgPaint;
    clipBgPaint.setStyle(Paint::Style::FILL);
    clipBgPaint.setFillColor(Color(30, 90, 190, 255));

    Paint clipRedPaint;
    clipRedPaint.setStyle(Paint::Style::FILL);
    clipRedPaint.setFillColor(Color(250, 80, 80, 150));

    Paint clipGreenPaint;
    clipGreenPaint.setStyle(Paint::Style::FILL);
    clipGreenPaint.setFillColor(Color(80, 220, 140, 150));

    Paint clipBorderPaint;
    clipBorderPaint.setStyle(Paint::Style::STROKE);
    clipBorderPaint.setStrokeColor(Color::WHITE);
    clipBorderPaint.setStrokeWidth(2.0f);

    Paint clipBoundsPaint;
    clipBoundsPaint.setStyle(Paint::Style::STROKE);
    clipBoundsPaint.setStrokeColor(Color(255, 245, 120, 220));
    clipBoundsPaint.setStrokeWidth(1.5f);

    Paint clipQueryPaint;
    clipQueryPaint.setStyle(Paint::Style::STROKE);
    clipQueryPaint.setStrokeColor(Color(80, 255, 170, 240));
    clipQueryPaint.setStrokeWidth(8.0f);

    Paint imagePaint;
    imagePaint.setColor(Color(255, 255, 255, 210));

    Paint tintedImagePaint;
    tintedImagePaint.setColor(Color(120, 220, 255, 190));
    tintedImagePaint.setImageSampling(Paint::ImageSampling::NEAREST);

    Paint filteredImagePaint;
    filteredImagePaint.setColor(Color(255, 255, 255, 220));
    filteredImagePaint.setImageSampling(Paint::ImageSampling::MIPMAP_LINEAR);
    filteredImagePaint.setColorMatrix(std::array<float, 20>{
        0.393f, 0.769f, 0.189f, 0.0f, 0.0f,
        0.349f, 0.686f, 0.168f, 0.0f, 0.0f,
        0.272f, 0.534f, 0.131f, 0.0f, 0.0f,
        0.0f,   0.0f,   0.0f,   1.0f, 0.0f
    });

    Paint tiledImagePaint;
    tiledImagePaint.setColor(Color(255, 255, 255, 180));
    tiledImagePaint.setImageSampling(Paint::ImageSampling::NEAREST);
    tiledImagePaint.setImageTileMode(Paint::ImageTileMode::MIRROR);

    Paint decalImagePaint;
    decalImagePaint.setColor(Color(255, 255, 255, 180));
    decalImagePaint.setImageSampling(Paint::ImageSampling::NEAREST);
    decalImagePaint.setImageTileMode(Paint::ImageTileMode::DECAL);

    Paint textPaint;
    textPaint.setStyle(Paint::Style::FILL);
    textPaint.setColor(Color(255, 245, 180, 220));
    textPaint.setTextSize(12.0f);
    textPaint.setFontFamily("Consolas");

    Paint rotatingTextPaint;
    rotatingTextPaint.setStyle(Paint::Style::FILL);
    rotatingTextPaint.setColor(Color(120, 230, 255, 200));
    rotatingTextPaint.setTextSize(14.4f);
    rotatingTextPaint.setFontFamily("Segoe UI");
    rotatingTextPaint.setLetterSpacing(1.2f);
    rotatingTextPaint.setTextAlign(Paint::TextAlign::CENTER);
    rotatingTextPaint.setTextBaseline(Paint::TextBaseline::MIDDLE);

    Paint pathTextPaint;
    pathTextPaint.setStyle(Paint::Style::FILL);
    pathTextPaint.setColor(Color(255, 255, 255, 210));
    pathTextPaint.setTextSize(10.0f);
    pathTextPaint.setFontFamily("Georgia");
    pathTextPaint.setLetterSpacing(1.0f);

    Image demoImage;
    bool imageLoaded = canvas.loadImage(demoImage, "images/hello.png");
    if (!imageLoaded) {
        imageLoaded = canvas.loadImage(demoImage, "images/draw_path.png");
    }

    ValidationImages validationImages;
    if (runImageValidation) {
        const std::vector<unsigned char> checkerPixels = makeValidationTexture(64, 64, 0);
        const std::vector<unsigned char> bandPixels = makeValidationTexture(96, 48, 1);
        validationImages.checkerLoaded = canvas.loadImageFromRGBA(validationImages.checker, checkerPixels, 64, 64, true);
        validationImages.bandsLoaded = canvas.loadImageFromRGBA(validationImages.bands, bandPixels, 96, 48, true);
        const std::vector<unsigned char> patchPixels = makeValidationTexture(16, 16, 1);
        validationImages.checkerLoaded = validationImages.checkerLoaded
            && canvas.updateImageRGBA(validationImages.checker, patchPixels, 24, 24, 16, 16, true);
        const std::vector<unsigned char> replacementPixels = makeValidationTexture(96, 48, 0);
        validationImages.bandsLoaded = validationImages.bandsLoaded
            && canvas.replaceImageRGBA(validationImages.bands, replacementPixels, 96, 48, true);
    }

    std::vector<PointF> demoPolylinePoints = {
        PointF(80.0f, 260.0f),
        PointF(130.0f, 230.0f),
        PointF(130.0f, 230.0f),
        PointF(200.0f, 280.0f),
        PointF(260.0f, 230.0f)
    };

    std::vector<Point> demoPolygonPoints = {
        Point(320, 470),
        Point(410, 420),
        Point(470, 500),
        Point(380, 560),
        Point(320, 470)
    };

    Path polygonPath;
    polygonPath.moveTo(320.0f, 470.0f);
    polygonPath.lineTo(410.0f, 420.0f);
    polygonPath.lineTo(470.0f, 500.0f);
    polygonPath.lineTo(380.0f, 560.0f);
    polygonPath.close();
    const auto polygonContourBounds = polygonPath.getContourBounds();

    Path demoPath;
    demoPath.moveTo(80.0f, 80.0f);
    demoPath.lineTo(230.0f, 80.0f);
    demoPath.lineTo(200.0f, 170.0f);
    demoPath.lineTo(110.0f, 190.0f);
    demoPath.close();

    Path concavePath;
    concavePath.moveTo(285.0f, 70.0f);
    concavePath.lineTo(455.0f, 70.0f);
    concavePath.lineTo(410.0f, 125.0f);
    concavePath.lineTo(455.0f, 190.0f);
    concavePath.lineTo(285.0f, 190.0f);
    concavePath.lineTo(335.0f, 125.0f);
    concavePath.close();

    Path evenOddPath;
    evenOddPath.setFillType(Path::FillType::EVEN_ODD);
    evenOddPath.addOval(RectF(690.0f, 245.0f, 72.0f, 72.0f));
    evenOddPath.addRoundRect(RectF(710.0f, 265.0f, 32.0f, 32.0f), 6.0f, 12.0f, 6.0f, 12.0f);
    const RectF evenOddBounds = evenOddPath.getBounds();
    const bool evenOddOuterHit = evenOddPath.contains(700.0f, 280.0f);
    const bool evenOddHoleHit = evenOddPath.contains(726.0f, 281.0f);

    Paint evenOddOuterHitPaint;
    evenOddOuterHitPaint.setStyle(Paint::Style::STROKE);
    evenOddOuterHitPaint.setStrokeWidth(10.0f);
    evenOddOuterHitPaint.setStrokeColor(evenOddOuterHit ? Color(80, 255, 140) : Color(255, 80, 80));

    Paint evenOddHoleHitPaint;
    evenOddHoleHitPaint.setStyle(Paint::Style::STROKE);
    evenOddHoleHitPaint.setStrokeWidth(10.0f);
    evenOddHoleHitPaint.setStrokeColor(evenOddHoleHit ? Color(80, 255, 140) : Color(255, 80, 80));

    Path roundStrokePath;
    roundStrokePath.moveTo(90.0f, 360.0f);
    roundStrokePath.lineTo(180.0f, 270.0f);
    roundStrokePath.lineTo(260.0f, 380.0f);
    roundStrokePath.lineTo(350.0f, 290.0f);
    roundStrokePath.lineTo(430.0f, 410.0f);
    const float roundStrokeLength = roundStrokePath.length();
    const RectF roundStrokeBounds = canvas.measureStrokeBounds(roundStrokePath, roundStrokePaint);
    const bool roundStrokeHit = roundStrokePath.strokeContains(180.0f, 270.0f, roundStrokePaint.getStrokeWidth());
    const bool roundStrokeMiss = roundStrokePath.strokeContains(180.0f, 235.0f, roundStrokePaint.getStrokeWidth());

    Paint roundStrokeHitPaint;
    roundStrokeHitPaint.setStyle(Paint::Style::STROKE);
    roundStrokeHitPaint.setStrokeWidth(9.0f);
    roundStrokeHitPaint.setStrokeColor(roundStrokeHit ? Color(80, 255, 140) : Color(255, 80, 80));

    Paint roundStrokeMissPaint;
    roundStrokeMissPaint.setStyle(Paint::Style::STROKE);
    roundStrokeMissPaint.setStrokeWidth(9.0f);
    roundStrokeMissPaint.setStrokeColor(roundStrokeMiss ? Color(255, 80, 80) : Color(80, 255, 140));

    Paint pathMetricPointPaint;
    pathMetricPointPaint.setStyle(Paint::Style::STROKE);
    pathMetricPointPaint.setStrokeColor(Color(120, 230, 255, 245));
    pathMetricPointPaint.setStrokeWidth(10.0f);

    Paint pathMetricTangentPaint;
    pathMetricTangentPaint.setStyle(Paint::Style::STROKE);
    pathMetricTangentPaint.setStrokeColor(Color(120, 230, 255, 180));
    pathMetricTangentPaint.setStrokeWidth(3.0f);

    Paint pathTrimPaint;
    pathTrimPaint.setStyle(Paint::Style::STROKE);
    pathTrimPaint.setStrokeColor(Color(255, 245, 120, 230));
    pathTrimPaint.setStrokeWidth(6.0f);
    pathTrimPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    pathTrimPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint reversePathTrimPaint;
    reversePathTrimPaint.setStyle(Paint::Style::STROKE);
    reversePathTrimPaint.setStrokeColor(Color(255, 120, 220, 220));
    reversePathTrimPaint.setStrokeWidth(4.0f);
    reversePathTrimPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    reversePathTrimPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Path degeneratePath;
    degeneratePath.moveTo(470.0f, 120.0f);
    degeneratePath.lineTo(470.0f, 120.0f);
    degeneratePath.lineTo(560.0f, 120.0f);
    degeneratePath.lineTo(560.0f, 120.0f);
    degeneratePath.lineTo(630.0f, 190.0f);

    Path curvePath;
    curvePath.moveTo(40.0f, 230.0f);
    curvePath.quadTo(150.0f, 120.0f, 260.0f, 230.0f);
    curvePath.cubicTo(330.0f, 310.0f, 390.0f, 130.0f, 470.0f, 235.0f);

    Path transformRectPath;
    transformRectPath.addRect(RectF(-60.0f, -40.0f, 120.0f, 80.0f));

    const Canvas::TextMetrics rotatingTextMetrics = canvas.measureTextMetrics("Rotating Text", rotatingTextPaint);
    const float rotatingTextClipHalfWidth = rotatingTextMetrics.width * 0.5f + 14.0f;
    const float rotatingTextClipHeight = rotatingTextMetrics.height + 24.0f;
    
    // Animation parameters
    const float centerX = 400.0f;
    const float centerY = 300.0f;
    const float radius = 100.0f;
    const int numPoints = 5;
    const float rotationSpeed = 1.0f;
    const float colorSpeed = 0.5f;  // color animation speed
    bool pixelReadbackChecked = false;
    bool captureChecked = false;
    const std::string capturePath = getEnvironmentValue("WHATSCANVAS_CAPTURE_PPM");
    const bool printPixelHash = !getEnvironmentValue("WHATSCANVAS_PRINT_PIXEL_HASH").empty();
    const bool exitAfterFirstFrame = !getEnvironmentValue("WHATSCANVAS_EXIT_AFTER_FIRST_FRAME").empty();
    const std::string expectedPixelHashText = getEnvironmentValue("WHATSCANVAS_EXPECT_PIXEL_HASH");
    const std::string fixedTimeText = getEnvironmentValue("WHATSCANVAS_FIXED_TIME_SECONDS");
    float fixedTimeSeconds = 0.0f;
    const bool hasFixedTime = parseFloat(fixedTimeText, fixedTimeSeconds);
    if (!fixedTimeText.empty() && !hasFixedTime) {
        std::cerr << "Fixed time invalid" << std::endl;
    }
    
    // Main loop
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        
        float currentTime = hasFixedTime ? fixedTimeSeconds : static_cast<float>(glfwGetTime());
        float rotation = currentTime * rotationSpeed;
        
        // Compute animated color
        float r = (sin(currentTime * colorSpeed) + 1.0f) * 0.5f;
        float g = (sin(currentTime * colorSpeed + 2.0f * PI / 3.0f) + 1.0f) * 0.5f;
        float b = (sin(currentTime * colorSpeed + 4.0f * PI / 3.0f) + 1.0f) * 0.5f;
        paint1.setColor(r, g, b);
        
        canvas.beginFrame();
        canvas.drawColor(Color(6, 8, 14));

        if (runTextValidation) {
            drawTextHeavyValidationScene(canvas, currentTime);
        } else if (runImageValidation) {
            drawImageHeavyValidationScene(canvas, validationImages, currentTime);
        } else {
        canvas.saveLayer(RectF(28.0f, 308.0f, 178.0f, 102.0f), saveLayerPaint);
        canvas.drawCircle(PointF(88.0f, 360.0f), 44.0f, layerCirclePaint);
        canvas.drawRoundRect(RectF(86.0f, 326.0f, 106.0f, 68.0f), 18.0f, 36.0f, 14.0f, 28.0f, layerRectPaint);
        canvas.restore();

        canvas.saveLayer(RectF(222.0f, 308.0f, 134.0f, 102.0f), saveLayerPaint);
        canvas.drawRoundRect(RectF(238.0f, 326.0f, 92.0f, 58.0f), 16.0f, porterDuffBasePaint);
        canvas.drawCircle(PointF(292.0f, 354.0f), 43.0f, porterDuffSrcInPaint);
        canvas.drawCircle(PointF(322.0f, 354.0f), 22.0f, porterDuffDstOutPaint);
        canvas.restore();
        canvas.drawRect(RectF(222.0f, 308.0f, 134.0f, 102.0f), clipBoundsPaint);

        canvas.save();
        Path clipPath;
        if (exerciseClipPath) {
            clipPath.addOval(RectF(520.0f, 420.0f, 220.0f, 150.0f));
            canvas.clipPath(clipPath);

            Path nestedClipPath;
            nestedClipPath.addOval(RectF(560.0f, 405.0f, 140.0f, 180.0f));
            canvas.clipPath(nestedClipPath);
        } else {
            clipPath.addRect(RectF(520.0f, 420.0f, 220.0f, 150.0f));
            canvas.clipPath(clipPath);
        }
        canvas.drawRect(RectF(500.0f, 400.0f, 260.0f, 180.0f), clipBgPaint);
        canvas.drawRect(RectF(540.0f, 440.0f, 120.0f, 120.0f), clipRedPaint);
        canvas.drawRect(RectF(600.0f, 450.0f, 120.0f, 120.0f), clipGreenPaint);
        RectF liveClipBounds;
        if (canvas.getClipBounds(liveClipBounds)) {
            canvas.drawRect(liveClipBounds, clipBoundsPaint);
        }
        if (canvas.isPointInClip(PointF(532.0f, 432.0f))
            && canvas.quickReject(RectF(760.0f, 450.0f, 40.0f, 40.0f))
            && canvas.quickReject(roundStrokePath, roundStrokePaint)) {
            canvas.drawPoint(532.0f, 432.0f, clipQueryPaint);
        }
        canvas.restore();
        canvas.drawRect(RectF(520.0f, 420.0f, 220.0f, 150.0f), clipBorderPaint);

        const int transformSave = canvas.save();
        canvas.translate(220.0f, 120.0f);
        canvas.rotate(rotation);
        canvas.scale(1.1f, 1.1f);
        const bool transformHit = canvas.hitTestPathFill(transformRectPath, PointF(220.0f, 120.0f));
        canvas.drawRect(RectF(-60.0f, -40.0f, 120.0f, 80.0f), rectPaint);
        canvas.restoreToCount(transformSave);
        transformHitPaint.setStrokeColor(transformHit ? Color(80, 255, 140) : Color(255, 80, 80));
        canvas.drawPoint(220.0f, 120.0f, transformHitPaint);

        canvas.drawPath(demoPath, pathPaint);
        canvas.drawPath(concavePath, concavePaint);
        canvas.drawPath(evenOddPath, evenOddPaint);
        canvas.drawRect(evenOddBounds, clipBorderPaint);
        canvas.drawPoint(700.0f, 280.0f, evenOddOuterHitPaint);
        canvas.drawPoint(726.0f, 281.0f, evenOddHoleHitPaint);
        canvas.drawPath(curvePath, curvePaint);
        canvas.drawTextOnPath("PATH TEXT", curvePath, 18.0f, -12.0f, pathTextPaint);
        canvas.drawPath(roundStrokePath, roundStrokePaint);
        canvas.drawRect(roundStrokeBounds, clipBoundsPaint);
        canvas.drawPoint(180.0f, 270.0f, roundStrokeHitPaint);
        canvas.drawPoint(180.0f, 235.0f, roundStrokeMissPaint);
        PointF metricPoint;
        PointF metricTangent;
        if (roundStrokeLength > 0.0f && roundStrokePath.pointAndTangentAtLength(std::fmod(currentTime * 90.0f, roundStrokeLength), metricPoint, metricTangent)) {
            const float trimStart = currentTime * 85.0f;
            const Path trimmedPath = roundStrokePath.trim(trimStart, trimStart + 180.0f, true);
            canvas.drawPath(trimmedPath, pathTrimPaint);
            const Path reverseTrimmedPath = roundStrokePath.trim(trimStart + 150.0f, trimStart, true, true);
            canvas.drawPath(reverseTrimmedPath, reversePathTrimPaint);
            canvas.drawPoint(metricPoint, pathMetricPointPaint);
            canvas.drawLine(metricPoint.getX(), metricPoint.getY(),
                            metricPoint.getX() + metricTangent.getX() * 28.0f,
                            metricPoint.getY() + metricTangent.getY() * 28.0f,
                            pathMetricTangentPaint);
        }
        canvas.drawPath(degeneratePath, roundStrokePaint);
        canvas.drawRect(RectF(500.0f, 300.0f, 190.0f, 130.0f), gradientRectPaint);
        canvas.drawRoundRect(RectF(500.0f, 80.0f, 210.0f, 150.0f), 46.0f, roundRectPaint);
        canvas.drawRoundRect(RectF(575.0f, 335.0f, 165.0f, 72.0f), 8.0f, 36.0f, 18.0f, 48.0f, asymmetricRoundRectPaint);
        canvas.drawCircle(PointF(610.0f, 505.0f), 62.0f, circlePaint);
        canvas.drawOval(RectF(80.0f, 440.0f, 220.0f, 110.0f), ovalPaint);
        canvas.drawArc(RectF(680.0f, 455.0f, 90.0f, 100.0f), -0.8f * PI, 1.45f * PI, false, arcPaint);
        canvas.drawLine(518.0f, 252.0f, 760.0f, 252.0f, dashedPaint);
        canvas.drawPolyline(demoPolylinePoints, pathPaint);
        canvas.drawPolygon(demoPolygonPoints, rectPaint);
        canvas.drawPath(polygonPath, roundedCornerPathPaint);
        if (!polygonContourBounds.empty()) {
            canvas.drawRect(polygonContourBounds.front(), clipBoundsPaint);
        }
        if (polygonPath.isClosed() && polygonPath.getClosedContourCount() == polygonPath.getContourCount()) {
            canvas.drawPath(polygonPath, closedDashPaint);
        }

        if (imageLoaded) {
            canvas.save();
            canvas.clipRect(RectF(320.0f, 20.0f, 160.0f, 120.0f));
            canvas.translate(400.0f, 80.0f);
            canvas.rotate(rotation * 0.25f);
            canvas.drawImage(demoImage, RectF(-80.0f, -60.0f, 160.0f, 120.0f), imagePaint);
            canvas.restore();

            const float srcW = static_cast<float>(demoImage.getWidth()) * 0.5f;
            const float srcH = static_cast<float>(demoImage.getHeight()) * 0.5f;
            canvas.drawImage(
                demoImage,
                RectF(0.0f, 0.0f, srcW, srcH),
                RectF(20.0f, 20.0f, 110.0f, 80.0f),
                tintedImagePaint);
            canvas.drawImage(
                demoImage,
                RectF(0.0f, 0.0f, srcW, srcH),
                RectF(140.0f, 20.0f, 110.0f, 80.0f),
                filteredImagePaint);
            canvas.drawImageNinePatch(
                demoImage,
                RectF(srcW * 0.45f, srcH * 0.45f, srcW * 0.6f, srcH * 0.6f),
                RectF(20.0f, 112.0f, 160.0f, 44.0f),
                imagePaint);
            canvas.drawImageFit(demoImage, RectF(190.0f, 112.0f, 58.0f, 44.0f), Canvas::ImageFit::COVER, Canvas::ImageAnchor::TOP, filteredImagePaint);
            canvas.drawImageFit(demoImage, RectF(252.0f, 112.0f, 52.0f, 44.0f), Canvas::ImageFit::CONTAIN, 1.0f, 0.0f, imagePaint);
            canvas.drawImageTiled(demoImage, RectF(260.0f, 20.0f, 44.0f, 80.0f), 16.0f, 16.0f, tiledImagePaint);
            canvas.drawImageTiled(demoImage, RectF(310.0f, 20.0f, 44.0f, 80.0f), 16.0f, 16.0f, decalImagePaint);
        }

            canvas.drawTextBox("Batch74: drawTextBox wraps ASCII text, caps visible rows, and ellipsizes overflow for compact panels.",
                               RectF(24.0f, 562.0f, 330.0f, 34.0f), 14.0f, 2, true, textPaint);

            canvas.save();
            canvas.translate(560.0f, 260.0f);
            canvas.rotate(-rotation * 0.35f);
            const RectF rotatingTextClipRect(-rotatingTextClipHalfWidth, -rotatingTextClipHeight * 0.5f,
                                             rotatingTextClipHalfWidth * 2.0f, rotatingTextClipHeight);
            const RectF rotatingTextDeviceBounds = canvas.mapRect(rotatingTextClipRect);
            canvas.clipRect(rotatingTextClipRect);
            canvas.drawText("Rotating Text", 0.0f, 10.0f, rotatingTextPaint);
            canvas.restore();
            canvas.drawRect(rotatingTextDeviceBounds, clipBoundsPaint);
        
        // Compute and store vertices
        std::vector<std::pair<float, float>> points;
        for (int i = 0; i < numPoints; i++) {
            float angle = rotation + i * (2 * PI / numPoints);
            float x = centerX + radius * cos(angle);
            float y = centerY + radius * sin(angle);
            points.push_back({x, y});
        }
        
        // Draw the lines
        for (int i = 0; i < numPoints; i++) {
            int next = (i + 2) % numPoints;
            canvas.drawLine(
                points[i].first, points[i].second,
                points[next].first, points[next].second,
                paint1
            );
            canvas.drawPoint(points[i].first, points[i].second, paint1);
        }
        }
        
        canvas.endFrame();
        if (!pixelReadbackChecked) {
            std::vector<unsigned char> pixels;
            const bool readbackOk = canvas.readPixelsRGBA(pixels);
            const size_t expectedPixelBytes = static_cast<size_t>(canvas.getWidth()) * static_cast<size_t>(canvas.getHeight()) * 4;
            if (!readbackOk || pixels.size() != expectedPixelBytes) {
                std::cerr << "Pixel readback failed" << std::endl;
            } else {
                const std::uint64_t bufferHash = Canvas::hashPixelsRGBA(pixels);
                const std::uint64_t framebufferHash = canvas.computePixelsHashRGBA();
                if (bufferHash != framebufferHash) {
                    std::cerr << "Pixel hash mismatch" << std::endl;
                }
                if (!expectedPixelHashText.empty()) {
                    std::uint64_t expectedPixelHash = 0;
                    if (!parseUint64(expectedPixelHashText, expectedPixelHash)) {
                        std::cerr << "Pixel hash expectation invalid" << std::endl;
                    } else if (bufferHash != expectedPixelHash) {
                        std::cerr << "Pixel hash mismatch expected=" << expectedPixelHash
                                  << " actual=" << bufferHash << std::endl;
                    }
                }
                if (printPixelHash) {
                    std::cout << "PIXEL_HASH_RGBA=" << bufferHash << std::endl;
                }
            }
            pixelReadbackChecked = true;
        }
        if (!captureChecked && !capturePath.empty()) {
            if (!canvas.savePixelsPPM(capturePath)) {
                std::cerr << "PPM capture failed" << std::endl;
            }
            captureChecked = true;
        }
        if (exitAfterFirstFrame && pixelReadbackChecked && (captureChecked || capturePath.empty())) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    canvas.shutdown();

    // Terminate GLFW
    glfwTerminate();
    std::cout << "GLFW terminated. Exiting application." << std::endl;

    return 0;
}
