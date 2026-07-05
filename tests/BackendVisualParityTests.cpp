// Backend visual parity smoke: render the same command stream through OpenGL
// and Vulkan, read both images back, and compare fuzzy pixel metrics.
// This is intentionally narrower than the full showcase scene so it stays
// stable across drivers while still covering solid fills, gradients, alpha
// blending, and text-geometry shader paths.

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "command/DrawCommand.h"
#include "command/DrawData.h"
#include "render/OpenGLRenderDevice.h"
#include "render/Renderer.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

constexpr int kWidth = 96;
constexpr int kHeight = 64;

struct DiffMetrics
{
    int maxChannelDelta = 0;
    int maxRgbDelta = 0;
    int maxAlphaDelta = 0;
    double meanChannelDelta = 0.0;
    double meanRgbDelta = 0.0;
    double changedPixelPercent = 0.0;
    double changedRgbPixelPercent = 0.0;
};

std::vector<float> quad(float x0, float y0, float x1, float y1)
{
    return {
        x0, y0, x1, y0, x1, y1,
        x0, y0, x1, y1, x0, y1,
    };
}

DrawPathData solidQuad(float x0, float y0, float x1, float y1, float r, float g, float b, float a,
                       DrawBlendMode blendMode = DrawBlendMode::SrcOver)
{
    DrawPathData d;
    d.points = quad(x0, y0, x1, y1);
    d.color[0] = r;
    d.color[1] = g;
    d.color[2] = b;
    d.color[3] = a;
    d.drawMode = PathDrawMode::Fill;
    d.capStyle = PathCapStyle::Round;
    d.blendMode = blendMode;
    return d;
}

DrawPathData gradientQuad(float x0, float y0, float x1, float y1)
{
    DrawPathData d = solidQuad(x0, y0, x1, y1, 1.0f, 1.0f, 1.0f, 1.0f);
    d.gradientType = DrawGradientType::Linear;
    d.gradientTileMode = DrawGradientTileMode::Clamp;
    d.gradientStart[0] = x0;
    d.gradientStart[1] = y0;
    d.gradientEnd[0] = x1;
    d.gradientEnd[1] = y0;
    d.gradientStopCount = 3;
    d.gradientStopPositions[0] = 0.0f;
    d.gradientStopPositions[1] = 0.5f;
    d.gradientStopPositions[2] = 1.0f;
    d.gradientStopColors[0] = 1.0f;
    d.gradientStopColors[3] = 1.0f;
    d.gradientStopColors[5] = 1.0f;
    d.gradientStopColors[7] = 1.0f;
    d.gradientStopColors[10] = 1.0f;
    d.gradientStopColors[11] = 1.0f;
    return d;
}

DrawTextData textGeometryQuad(float x0, float y0, float x1, float y1)
{
    DrawTextData d;
    d.vertices = quad(x0, y0, x1, y1);
    d.gradientType = DrawGradientType::Linear;
    d.gradientTileMode = DrawGradientTileMode::Clamp;
    d.gradientStart[0] = x0;
    d.gradientStart[1] = y0;
    d.gradientEnd[0] = x1;
    d.gradientEnd[1] = y0;
    d.gradientStopCount = 2;
    d.gradientStopPositions[0] = 0.0f;
    d.gradientStopPositions[1] = 1.0f;
    d.gradientStopColors[0] = 1.0f;
    d.gradientStopColors[1] = 0.9f;
    d.gradientStopColors[3] = 1.0f;
    d.gradientStopColors[6] = 1.0f;
    d.gradientStopColors[7] = 1.0f;
    return d;
}

std::vector<std::unique_ptr<Command>> makeParityCommands()
{
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawPathCommand>(
        solidQuad(0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight),
                  0.04f, 0.05f, 0.09f, 1.0f, DrawBlendMode::Src)));
    commands.push_back(std::make_unique<DrawPathCommand>(
        solidQuad(8.0f, 8.0f, 40.0f, 30.0f, 0.92f, 0.18f, 0.12f, 1.0f)));
    commands.push_back(std::make_unique<DrawPathCommand>(
        solidQuad(24.0f, 18.0f, 56.0f, 42.0f, 0.10f, 0.72f, 0.95f, 0.58f)));
    commands.push_back(std::make_unique<DrawPathCommand>(gradientQuad(54.0f, 8.0f, 90.0f, 36.0f)));

    commands.push_back(std::make_unique<DrawTextCommand>(textGeometryQuad(58.0f, 44.0f, 88.0f, 56.0f)));
    return commands;
}

DiffMetrics compareRGBA(const std::vector<unsigned char> &reference, const std::vector<unsigned char> &candidate)
{
    DiffMetrics metrics;
    if (reference.size() != candidate.size() || reference.empty()) {
        metrics.maxChannelDelta = 255;
        metrics.meanChannelDelta = 255.0;
        metrics.changedPixelPercent = 100.0;
        return metrics;
    }

    std::uint64_t totalDelta = 0;
    std::uint64_t totalRgbDelta = 0;
    int changedPixels = 0;
    int changedRgbPixels = 0;
    const std::size_t pixelCount = reference.size() / 4u;
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        bool changed = false;
        bool rgbChanged = false;
        const std::size_t base = pixel * 4u;
        for (std::size_t channel = 0; channel < 4u; ++channel) {
            const int delta = std::abs(static_cast<int>(reference[base + channel])
                                       - static_cast<int>(candidate[base + channel]));
            metrics.maxChannelDelta = std::max(metrics.maxChannelDelta, delta);
            if (channel < 3u) {
                metrics.maxRgbDelta = std::max(metrics.maxRgbDelta, delta);
                totalRgbDelta += static_cast<std::uint64_t>(delta);
                rgbChanged = rgbChanged || delta != 0;
            } else {
                metrics.maxAlphaDelta = std::max(metrics.maxAlphaDelta, delta);
            }
            totalDelta += static_cast<std::uint64_t>(delta);
            changed = changed || delta != 0;
        }
        if (changed) {
            ++changedPixels;
        }
        if (rgbChanged) {
            ++changedRgbPixels;
        }
    }

    metrics.meanChannelDelta = static_cast<double>(totalDelta) / static_cast<double>(reference.size());
    metrics.meanRgbDelta = static_cast<double>(totalRgbDelta) / static_cast<double>(pixelCount * 3u);
    metrics.changedPixelPercent = static_cast<double>(changedPixels) * 100.0 / static_cast<double>(pixelCount);
    metrics.changedRgbPixelPercent = static_cast<double>(changedRgbPixels) * 100.0 / static_cast<double>(pixelCount);
    return metrics;
}

bool writePPM(const std::string &path, const std::vector<unsigned char> &rgba)
{
    if (path.empty() || rgba.size() != static_cast<std::size_t>(kWidth) * static_cast<std::size_t>(kHeight) * 4u) {
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "P6\n" << kWidth << " " << kHeight << "\n255\n";
    for (std::size_t i = 0; i < rgba.size(); i += 4u) {
        const char rgb[3] = {
            static_cast<char>(rgba[i]),
            static_cast<char>(rgba[i + 1u]),
            static_cast<char>(rgba[i + 2u]),
        };
        out.write(rgb, 3);
    }
    return true;
}

std::vector<unsigned char> makeDiffImage(const std::vector<unsigned char> &reference,
                                         const std::vector<unsigned char> &candidate)
{
    std::vector<unsigned char> diff(reference.size(), 255);
    if (reference.size() != candidate.size()) {
        return diff;
    }
    for (std::size_t i = 0; i < reference.size(); i += 4u) {
        diff[i] = static_cast<unsigned char>(std::min(255, std::abs(static_cast<int>(reference[i])
                                                                    - static_cast<int>(candidate[i])) * 8));
        diff[i + 1u] = static_cast<unsigned char>(std::min(255, std::abs(static_cast<int>(reference[i + 1u])
                                                                         - static_cast<int>(candidate[i + 1u])) * 8));
        diff[i + 2u] = static_cast<unsigned char>(std::min(255, std::abs(static_cast<int>(reference[i + 2u])
                                                                         - static_cast<int>(candidate[i + 2u])) * 8));
        diff[i + 3u] = 255;
    }
    return diff;
}

bool renderOpenGL(std::vector<unsigned char> &pixels)
{
    if (!glfwInit()) {
        std::cerr << "[BackendVisualParityTests] FAIL: glfwInit failed." << std::endl;
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, 0);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(kWidth, kHeight, "WhatsCanvas Backend Visual Parity", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "[BackendVisualParityTests] FAIL: glfwCreateWindow failed." << std::endl;
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        std::cerr << "[BackendVisualParityTests] FAIL: gladLoadGLLoader failed." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return false;
    }

    glViewport(0, 0, kWidth, kHeight);
    glDisable(GL_MULTISAMPLE);
    glDisable(GL_DITHER);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    Renderer renderer(std::make_unique<OpenGLRenderDevice>());
    renderer.initializeBackend();
    renderer.setViewport(kWidth, kHeight);
    renderer.appendCommands(makeParityCommands());
    renderer.flush();
    const bool ok = renderer.readPixelsRGBA(pixels);
    renderer.finalizeBackend();

    glfwDestroyWindow(window);
    glfwTerminate();
    return ok;
}

bool renderVulkan(std::vector<unsigned char> &pixels, std::string &deviceName)
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[BackendVisualParityTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return false;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[BackendVisualParityTests] FAIL: Vulkan device was not created." << std::endl;
        return false;
    }
    deviceName = device.selectedDeviceName();

    auto target = device.createRenderTarget(kWidth, kHeight);
    if (!target || !target->isValid()) {
        std::cerr << "[BackendVisualParityTests] FAIL: could not create Vulkan render target." << std::endl;
        return false;
    }

    OffscreenRenderRequest request;
    request.canvasWidth = kWidth;
    request.canvasHeight = kHeight;
    request.targetWidth = kWidth;
    request.targetHeight = kHeight;

    auto commands = makeParityCommands();
    if (!device.executeCommands(target, commands, request)) {
        std::cerr << "[BackendVisualParityTests] FAIL: Vulkan executeCommands returned false." << std::endl;
        return false;
    }
    const bool ok = device.readPixelsRGBA(kWidth, kHeight, pixels);
    commands.clear();
    target.reset();
    device.finalizeBackend();
    return ok;
}

} // namespace

int main()
{
    std::vector<unsigned char> glPixels;
    std::vector<unsigned char> vkPixels;
    std::string vulkanDevice;
    if (!renderOpenGL(glPixels)) {
        return 1;
    }
    if (!renderVulkan(vkPixels, vulkanDevice)) {
        return 1;
    }

    const DiffMetrics diff = compareRGBA(glPixels, vkPixels);
    std::cout << "BACKEND_VISUAL_PARITY_WIDTH=" << kWidth << std::endl;
    std::cout << "BACKEND_VISUAL_PARITY_HEIGHT=" << kHeight << std::endl;
    std::cout << "BACKEND_VISUAL_PARITY_VULKAN_DEVICE=" << vulkanDevice << std::endl;
    std::cout << "BACKEND_VISUAL_PARITY_MAX_CHANNEL_DELTA=" << diff.maxChannelDelta << std::endl;
    std::cout << "BACKEND_VISUAL_PARITY_MAX_RGB_DELTA=" << diff.maxRgbDelta << std::endl;
    std::cout << "BACKEND_VISUAL_PARITY_MAX_ALPHA_DELTA=" << diff.maxAlphaDelta << std::endl;
    std::cout << "BACKEND_VISUAL_PARITY_MEAN_CHANNEL_DELTA=" << diff.meanChannelDelta << std::endl;
    std::cout << "BACKEND_VISUAL_PARITY_MEAN_RGB_DELTA=" << diff.meanRgbDelta << std::endl;
    std::cout << "BACKEND_VISUAL_PARITY_CHANGED_PERCENT=" << diff.changedPixelPercent << std::endl;
    std::cout << "BACKEND_VISUAL_PARITY_CHANGED_RGB_PERCENT=" << diff.changedRgbPixelPercent << std::endl;

    constexpr int kMaxChannelDelta = 8;
    constexpr double kMaxMeanChannelDelta = 1.25;
    constexpr double kMaxChangedPercent = 12.0;
    const bool passed = diff.maxChannelDelta <= kMaxChannelDelta
        && diff.meanChannelDelta <= kMaxMeanChannelDelta
        && diff.changedPixelPercent <= kMaxChangedPercent;
    std::cout << "BACKEND_VISUAL_PARITY_RESULT=" << (passed ? "PASS" : "FAIL") << std::endl;
    if (!passed) {
        const char *outputDir = std::getenv("WHATSCANVAS_BACKEND_PARITY_OUTPUT_DIR");
        if (outputDir != nullptr && outputDir[0] != '\0') {
            const std::string base(outputDir);
            const std::string slash = (base.back() == '/' || base.back() == '\\') ? "" : "/";
            const std::string glPath = base + slash + "backend_parity_opengl.ppm";
            const std::string vkPath = base + slash + "backend_parity_vulkan.ppm";
            const std::string diffPath = base + slash + "backend_parity_diff.ppm";
            writePPM(glPath, glPixels);
            writePPM(vkPath, vkPixels);
            writePPM(diffPath, makeDiffImage(glPixels, vkPixels));
            std::cerr << "[BackendVisualParityTests] Wrote debug images to " << base << std::endl;
        }
        std::cerr << "[BackendVisualParityTests] FAIL: OpenGL/Vulkan visual diff exceeded thresholds." << std::endl;
        return 1;
    }

    return 0;
}
