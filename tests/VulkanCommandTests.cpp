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

    // Three compatible indexed paths exercise direct solid-path batching:
    // index rebasing, uniform-to-vertex color expansion, packed vertex colors,
    // and missing analytic coverage all need to remain aligned.
    const auto makeIndexedQuad = [height](
                                     float left, float right,
                                     float red, float green,
                                     float blue) {
        DrawPathData data;
        data.points = {
            left, 0.0f, right, 0.0f,
            right, static_cast<float>(height),
            left, static_cast<float>(height),
        };
        data.shortIndices = {0, 1, 2, 0, 2, 3};
        data.color[0] = red;
        data.color[1] = green;
        data.color[2] = blue;
        data.color[3] = 1.0f;
        data.drawMode = PathDrawMode::Fill;
        return data;
    };
    DrawPathData redQuad =
        makeIndexedQuad(0.0f, 20.0f, 1.0f, 0.0f, 0.0f);
    DrawPathData greenQuad =
        makeIndexedQuad(22.0f, 42.0f, 0.0f, 1.0f, 0.0f);
    greenQuad.packedCoverage = {255, 255, 255, 255};
    DrawPathData blueQuad =
        makeIndexedQuad(44.0f, 64.0f, 0.0f, 0.0f, 0.0f);
    blueQuad.packedColors = {
        0, 0, 255, 255, 0, 0, 255, 255,
        0, 0, 255, 255, 0, 0, 255, 255,
    };
    std::vector<std::unique_ptr<Command>> batchedCommands;
    batchedCommands.push_back(
        std::make_unique<DrawPathCommand>(redQuad));
    batchedCommands.push_back(
        std::make_unique<DrawPathCommand>(greenQuad));
    batchedCommands.push_back(
        std::make_unique<DrawPathCommand>(blueQuad));
    if (!device.executeCommands(
            target, batchedCommands, request)) {
        std::cerr
            << "[VulkanCommandTests] FAIL: executeCommands "
               "(indexed batch) returned false."
            << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr
            << "[VulkanCommandTests] FAIL: indexed batch "
               "readback failed."
            << std::endl;
        return 1;
    }
    if (!pixelIs(
            pixels, width, 10, height / 2,
            255, 0, 0, 255, "indexed batch red")) {
        return 1;
    }
    if (!pixelIs(
            pixels, width, 32, height / 2,
            0, 255, 0, 255, "indexed batch green")) {
        return 1;
    }
    if (!pixelIs(
            pixels, width, 54, height / 2,
            0, 0, 255, 255, "indexed batch blue")) {
        return 1;
    }

    // A full-canvas green quad restricted by a top-left Canvas-space scissor.
    // ScissorState stores a bottom-left Y for the command stream; the shared
    // encoder converts it to the top-left convention used by DrawPrimitive and
    // Vulkan.
    DrawPathData scissoredData;
    scissoredData.points = {
        0.0f, 0.0f, static_cast<float>(width), 0.0f,
        static_cast<float>(width), static_cast<float>(height),
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
        0.0f, static_cast<float>(height),
    };
    scissoredData.color[0] = 0.0f;
    scissoredData.color[1] = 1.0f;
    scissoredData.color[2] = 0.0f;
    scissoredData.color[3] = 1.0f;
    scissoredData.drawMode = PathDrawMode::Fill;
    scissoredData.scissor.enabled = true;
    scissoredData.scissor.x = 10;
    scissoredData.scissor.y = height - 30;
    scissoredData.scissor.width = 20;
    scissoredData.scissor.height = 20;
    std::vector<std::unique_ptr<Command>> scissoredCommands;
    scissoredCommands.push_back(std::make_unique<DrawPathCommand>(scissoredData));
    if (!device.executeCommands(target, scissoredCommands, request)) {
        std::cerr << "[VulkanCommandTests] FAIL: executeCommands (scissor) returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanCommandTests] FAIL: scissor readback failed." << std::endl;
        return 1;
    }
    if (!pixelIs(pixels, width, 15, 15, 0, 255, 0, 255, "scissor inside green")) return 1;
    if (!pixelIs(pixels, width, 5, 15, 0, 0, 0, 0, "scissor left clear")) return 1;
    if (!pixelIs(pixels, width, 15, 35, 0, 0, 0, 0, "scissor bottom clear")) return 1;

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

    // Alpha-only image batches use the compact instanced glyph-atlas path.
    const std::vector<unsigned char> alphaTexel = {255};
    auto alphaImage =
        device.createImageResourceAlpha8(1, 1, alphaTexel);
    if (!alphaImage || !alphaImage->isValid()) {
        std::cerr
            << "[VulkanCommandTests] FAIL: could not create "
               "alpha batch image."
            << std::endl;
        return 1;
    }
    DrawImageBatchData alphaBatchData;
    alphaBatchData.imageResource = alphaImage;
    alphaBatchData.quads = {
        {4.0f, 4.0f, 20.0f, 16.0f,
         0.0f, 0.0f, 1.0f, 1.0f, 0xff0000ffu},
        {36.0f, 28.0f, 20.0f, 16.0f,
         0.0f, 0.0f, 1.0f, 1.0f, 0xff00ff00u},
    };
    std::vector<std::unique_ptr<Command>> alphaBatchCommands;
    alphaBatchCommands.push_back(
        std::make_unique<DrawImageBatchCommand>(alphaBatchData));
    if (!device.executeCommands(
            target, alphaBatchCommands, request)) {
        std::cerr
            << "[VulkanCommandTests] FAIL: executeCommands "
               "(alpha image batch) returned false."
            << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr
            << "[VulkanCommandTests] FAIL: alpha image batch "
               "readback failed."
            << std::endl;
        return 1;
    }
    if (!pixelIs(
            pixels, width, 12, 12, 255, 0, 0, 255,
            "alpha image batch first red")) {
        return 1;
    }
    if (!pixelIs(
            pixels, width, 44, 36, 0, 255, 0, 255,
            "alpha image batch second green")) {
        return 1;
    }
    if (!pixelIs(
            pixels, width, 30, 24, 0, 0, 0, 0,
            "alpha image batch gap clear")) {
        return 1;
    }

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
    imageData.sampling = DrawImageSampling::Nearest; // exact 2x2 quadrant checks
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

    // Different per-image alpha values are folded into packed vertex tints.
    // Non-overlapping rounded and square quads carry their clip parameters per
    // instance, so all four images become one draw without changing painter
    // order. Seven submissions also exercise descriptor-set reuse before the
    // final readback; busy GPUs rotate through the three frame slots while
    // already-signaled slots are reclaimed in place.
    const std::vector<unsigned char> whiteTexel = {
        255, 255, 255, 255};
    auto whiteImage =
        device.createImageResourceRGBA(1, 1, whiteTexel);
    if (!whiteImage || !whiteImage->isValid()) {
        std::cerr
            << "[VulkanCommandTests] FAIL: could not create "
               "batch image."
            << std::endl;
        return 1;
    }
    std::vector<std::unique_ptr<Command>> textureBatchCommands;
    constexpr float alphas[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    for (int column = 0; column < 4; ++column) {
        DrawImageData batchImageData;
        batchImageData.imageResource = whiteImage;
        batchImageData.x = static_cast<float>(column * 16);
        batchImageData.y = 0.0f;
        batchImageData.width = 14.0f;
        batchImageData.height = static_cast<float>(height);
        batchImageData.alpha = alphas[column];
        batchImageData.tintColor[0] = 1.0f;
        batchImageData.tintColor[1] = 0.0f;
        batchImageData.tintColor[2] = 0.0f;
        batchImageData.tintColor[3] = 1.0f;
        batchImageData.roundedRadius =
            (column % 2) == 0 ? 4.0f : 0.0f;
        batchImageData.sampling = DrawImageSampling::Nearest;
        textureBatchCommands.push_back(
            std::make_unique<DrawImageCommand>(batchImageData));
    }
    for (int frame = 0; frame < 7; ++frame) {
        if (!device.executeCommands(
                target, textureBatchCommands, request)) {
            std::cerr
                << "[VulkanCommandTests] FAIL: async texture "
                   "batch frame "
                << frame << " failed." << std::endl;
            return 1;
        }
        if (device.lastExecutionDrawCallCount() != 1u) {
            std::cerr
                << "[VulkanCommandTests] FAIL: texture batch "
                   "used "
                << device.lastExecutionDrawCallCount()
                << " draws, expected 1." << std::endl;
            return 1;
        }
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr
            << "[VulkanCommandTests] FAIL: async texture batch "
               "readback failed."
            << std::endl;
        return 1;
    }
    if (!pixelNear(
            pixels, width, 7, height / 2,
            64, 0, 0, 64, 2, "batch alpha 25%")) {
        return 1;
    }
    if (!pixelNear(
            pixels, width, 23, height / 2,
            128, 0, 0, 128, 2, "batch alpha 50%")) {
        return 1;
    }
    if (!pixelNear(
            pixels, width, 39, height / 2,
            191, 0, 0, 191, 2, "batch alpha 75%")) {
        return 1;
    }
    if (!pixelIs(
            pixels, width, 55, height / 2,
            255, 0, 0, 255, "batch alpha 100%")) {
        return 1;
    }

    // The recorded commands may be reused when only instance payload changes.
    // Verify that the persistent command buffer still consumes the newly
    // uploaded vertex data instead of retaining the previous frame's tint.
    std::vector<std::unique_ptr<Command>> updatedTextureBatchCommands;
    for (int column = 0; column < 4; ++column) {
        DrawImageData batchImageData;
        batchImageData.imageResource = whiteImage;
        batchImageData.x = static_cast<float>(column * 16);
        batchImageData.y = 0.0f;
        batchImageData.width = 14.0f;
        batchImageData.height = static_cast<float>(height);
        batchImageData.alpha = alphas[column];
        batchImageData.tintColor[0] = 0.0f;
        batchImageData.tintColor[1] = 1.0f;
        batchImageData.tintColor[2] = 0.0f;
        batchImageData.tintColor[3] = 1.0f;
        batchImageData.roundedRadius =
            (column % 2) == 0 ? 4.0f : 0.0f;
        batchImageData.sampling = DrawImageSampling::Nearest;
        updatedTextureBatchCommands.push_back(
            std::make_unique<DrawImageCommand>(batchImageData));
    }
    if (!device.executeCommands(
            target, updatedTextureBatchCommands, request)) {
        std::cerr
            << "[VulkanCommandTests] FAIL: updated texture "
               "batch frame failed." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr
            << "[VulkanCommandTests] FAIL: updated texture "
               "batch readback failed." << std::endl;
        return 1;
    }
    if (!pixelNear(
            pixels, width, 23, height / 2,
            0, 128, 0, 128, 2,
            "reused command buffer updated tint")) {
        return 1;
    }

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

    // renderCommandsToImageResource: render a command stream into an owned,
    // sampleable texture, then composite that texture to verify its content.
    DrawPathData layerPath;
    layerPath.points = {5.0f, 5.0f, 59.0f, 5.0f, 32.0f, 43.0f};
    layerPath.color[0] = 1.0f;
    layerPath.color[1] = 0.0f;
    layerPath.color[2] = 0.0f;
    layerPath.color[3] = 1.0f;
    layerPath.drawMode = PathDrawMode::Fill;
    layerPath.capStyle = PathCapStyle::Round;
    std::vector<std::unique_ptr<Command>> layerCommands;
    layerCommands.push_back(std::make_unique<DrawPathCommand>(layerPath));
    auto layerImage = device.renderCommandsToImageResource(layerCommands, request);
    if (!layerImage || !layerImage->isValid()) {
        std::cerr << "[VulkanCommandTests] FAIL: renderCommandsToImageResource returned no image." << std::endl;
        return 1;
    }
    auto dst4 = device.createRenderTarget(width, height);
    if (!dst4 || !dst4->isValid() || !device.renderTexturedQuad(dst4, layerImage)) {
        std::cerr << "[VulkanCommandTests] FAIL: could not draw the rendered command image." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanCommandTests] FAIL: layer-image readback failed." << std::endl;
        return 1;
    }
    if (!pixelIs(pixels, width, width / 2, height / 2, 255, 0, 0, 255, "layer center red")) return 1;
    if (!pixelIs(pixels, width, 0, 0, 0, 0, 0, 0, "layer corner clear")) return 1;

    std::cout << "[VulkanCommandTests] PASS: translated a real Command stream on \"" << device.selectedDeviceName()
              << "\"." << std::endl;

    commands.clear();
    batchedCommands.clear();
    scissoredCommands.clear();
    pointCommands.clear();
    lineCommands.clear();
    imageData.imageResource.reset();
    imageCommands.clear();
    textureBatchCommands.clear();
    whiteImage.reset();
    gradientCommands.clear();
    layerCommands.clear();
    layerImage.reset();
    image.reset();
    dst4.reset();
    target.reset();
    device.finalizeBackend();
    return 0;
}
