// ADR-006 command-translation slice: render a real WhatsCanvas Command stream on
// Vulkan. Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "command/DrawCommand.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

bool pixelIs(const std::vector<unsigned char> &pixels, int width, int x, int y, int r, int g, int b, int a,
             const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    if (idx + 3 >= pixels.size()) {
        std::cerr << "[VulkanCommandTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    if (pixels[idx] != r || pixels[idx + 1] != g || pixels[idx + 2] != b || pixels[idx + 3] != a) {
        std::cerr << "[VulkanCommandTests] FAIL: " << label << " = (" << int(pixels[idx]) << ","
                  << int(pixels[idx + 1]) << "," << int(pixels[idx + 2]) << "," << int(pixels[idx + 3])
                  << "), expected (" << r << "," << g << "," << b << "," << a << ")." << std::endl;
        return false;
    }
    return true;
}

bool pixelNear(const std::vector<unsigned char> &pixels, int width, int x, int y, int r, int g, int b, int a,
               int tol, const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    if (idx + 3 >= pixels.size()) {
        std::cerr << "[VulkanCommandTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    const int pr = pixels[idx], pg = pixels[idx + 1], pb = pixels[idx + 2], pa = pixels[idx + 3];
    if (std::abs(pr - r) > tol || std::abs(pg - g) > tol || std::abs(pb - b) > tol || std::abs(pa - a) > tol) {
        std::cerr << "[VulkanCommandTests] FAIL: " << label << " = (" << pr << "," << pg << "," << pb << "," << pa
                  << "), expected ~(" << r << "," << g << "," << b << "," << a << ") +/-" << tol << "." << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanCommandTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanCommandTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 64;
    const int height = 48;
    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanCommandTests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    // Build a real DrawPathCommand: a filled triangle (canvas-space, triangle
    // list) covering the center, in red.
    DrawPathData pathData;
    pathData.points = {5.0f, 5.0f, 59.0f, 5.0f, 32.0f, 43.0f};
    pathData.color[0] = 1.0f;
    pathData.color[1] = 0.0f;
    pathData.color[2] = 0.0f;
    pathData.color[3] = 1.0f;
    pathData.drawMode = PathDrawMode::Fill;
    pathData.capStyle = PathCapStyle::Round;

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawPathCommand>(pathData));

    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;

    if (!device.executeCommands(target, commands, request)) {
        std::cerr << "[VulkanCommandTests] FAIL: executeCommands returned false." << std::endl;
        return 1;
    }

    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanCommandTests] FAIL: readback failed." << std::endl;
        return 1;
    }
    // Center is inside the filled triangle -> red; top-left corner -> clear.
    if (!pixelIs(pixels, width, width / 2, height / 2, 255, 0, 0, 255, "center red")) return 1;
    if (!pixelIs(pixels, width, 0, 0, 0, 0, 0, 0, "corner clear")) return 1;

    // A points command: a large green point centered on the canvas.
    DrawPointsData pointsData;
    pointsData.points = {static_cast<float>(width) / 2.0f, static_cast<float>(height) / 2.0f};
    pointsData.size = 10.0f;
    pointsData.color[0] = 0.0f;
    pointsData.color[1] = 1.0f;
    pointsData.color[2] = 0.0f;
    pointsData.color[3] = 1.0f;
    std::vector<std::unique_ptr<Command>> pointCommands;
    pointCommands.push_back(std::make_unique<DrawPointsCommand>(pointsData));
    if (!device.executeCommands(target, pointCommands, request)) {
        std::cerr << "[VulkanCommandTests] FAIL: executeCommands (points) returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanCommandTests] FAIL: points readback failed." << std::endl;
        return 1;
    }
    if (!pixelIs(pixels, width, width / 2, height / 2, 0, 255, 0, 255, "point center green")) return 1;
    if (!pixelIs(pixels, width, 2, 2, 0, 0, 0, 0, "point off clear")) return 1;

    // A lines command: a thick horizontal blue line across the middle row.
    DrawLinesData linesData;
    linesData.points = {8.0f, static_cast<float>(height) / 2.0f, static_cast<float>(width) - 8.0f,
                        static_cast<float>(height) / 2.0f};
    linesData.width = 8.0f;
    linesData.color[0] = 0.0f;
    linesData.color[1] = 0.0f;
    linesData.color[2] = 1.0f;
    linesData.color[3] = 1.0f;
    std::vector<std::unique_ptr<Command>> lineCommands;
    lineCommands.push_back(std::make_unique<DrawLinesCommand>(linesData));
    if (!device.executeCommands(target, lineCommands, request)) {
        std::cerr << "[VulkanCommandTests] FAIL: executeCommands (lines) returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanCommandTests] FAIL: lines readback failed." << std::endl;
        return 1;
    }
    if (!pixelIs(pixels, width, width / 2, height / 2, 0, 0, 255, 255, "line center blue")) return 1;
    if (!pixelIs(pixels, width, width / 2, 2, 0, 0, 0, 0, "line off clear")) return 1;

    // A DrawImage command: a 2x2 texture drawn across the whole canvas.
    const std::vector<unsigned char> texels = {
        255, 0,   0,   255, 0,   255, 0,   255,
        0,   0,   255, 255, 255, 255, 0,   255,
    };
    auto image = device.createImageResourceRGBA(2, 2, texels);
    if (!image || !image->isValid()) {
        std::cerr << "[VulkanCommandTests] FAIL: could not create image." << std::endl;
        return 1;
    }
    DrawImageData imageData;
    imageData.imageResource = image;
    imageData.x = 0.0f;
    imageData.y = 0.0f;
    imageData.width = static_cast<float>(width);
    imageData.height = static_cast<float>(height);
    imageData.u0 = 0.0f;
    imageData.v0 = 0.0f;
    imageData.u1 = 1.0f;
    imageData.v1 = 1.0f;
    imageData.alpha = 1.0f;
    std::vector<std::unique_ptr<Command>> imageCommands;
    imageCommands.push_back(std::make_unique<DrawImageCommand>(imageData));
    if (!device.executeCommands(target, imageCommands, request)) {
        std::cerr << "[VulkanCommandTests] FAIL: executeCommands (image) returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanCommandTests] FAIL: image readback failed." << std::endl;
        return 1;
    }
    if (!pixelIs(pixels, width, 16, 12, 255, 0, 0, 255, "image top-left red")) return 1;
    if (!pixelIs(pixels, width, 48, 36, 255, 255, 0, 255, "image bottom-right yellow")) return 1;

    // A vertex-color path (baked gradient): a full-canvas quad, left red, right blue.
    DrawPathData gradientData;
    gradientData.points = {
        0.0f, 0.0f, static_cast<float>(width), 0.0f, static_cast<float>(width), static_cast<float>(height),
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, static_cast<float>(height),
    };
    gradientData.drawMode = PathDrawMode::Fill;
    gradientData.capStyle = PathCapStyle::Round;
    // Per-vertex colors: x==0 -> red, x==width -> blue.
    const float redV[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float blueV[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    for (std::size_t v = 0; v < gradientData.points.size() / 2; ++v) {
        const float *c = gradientData.points[v * 2] < 1.0f ? redV : blueV;
        gradientData.colors.insert(gradientData.colors.end(), c, c + 4);
    }
    std::vector<std::unique_ptr<Command>> gradientCommands;
    gradientCommands.push_back(std::make_unique<DrawPathCommand>(gradientData));
    if (!device.executeCommands(target, gradientCommands, request)) {
        std::cerr << "[VulkanCommandTests] FAIL: executeCommands (vertex-color) returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanCommandTests] FAIL: vertex-color readback failed." << std::endl;
        return 1;
    }
    if (!pixelNear(pixels, width, 0, height / 2, 255, 0, 0, 255, 10, "grad left red")) return 1;
    if (!pixelNear(pixels, width, width - 1, height / 2, 0, 0, 255, 255, 10, "grad right blue")) return 1;
    if (!pixelNear(pixels, width, width / 2, height / 2, 128, 0, 128, 255, 10, "grad center purple")) return 1;

    std::cout << "[VulkanCommandTests] PASS: translated a real Command stream on \"" << device.selectedDeviceName()
              << "\"." << std::endl;

    commands.clear();
    pointCommands.clear();
    lineCommands.clear();
    imageData.imageResource.reset();
    imageCommands.clear();
    gradientCommands.clear();
    image.reset();
    target.reset();
    device.finalizeBackend();
    return 0;
}
