// Text command translation: WhatsCanvas renders text as vector triangle
// geometry (glyph outlines tessellated into local-space triangles) filled with a
// solid color or the same shader gradient as paths -- there is no glyph atlas.
// This test drives DrawTextCommand through executeCommands and reads back pixels.
// Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "command/DrawCommand.h"
#include "command/DrawData.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

bool near4(const std::vector<unsigned char> &px, int width, int x, int y, int r, int g, int b, int a, int tol,
           const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    if (idx + 3 >= px.size()) {
        std::cerr << "[VulkanTextTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    const int pr = px[idx], pg = px[idx + 1], pb = px[idx + 2], pa = px[idx + 3];
    if (std::abs(pr - r) > tol || std::abs(pg - g) > tol || std::abs(pb - b) > tol || std::abs(pa - a) > tol) {
        std::cerr << "[VulkanTextTests] FAIL: " << label << " = (" << pr << "," << pg << "," << pb << "," << pa
                  << "), expected ~(" << r << "," << g << "," << b << "," << a << ") +/-" << tol << "."
                  << std::endl;
        return false;
    }
    return true;
}

std::vector<float> quad(float x0, float y0, float x1, float y1)
{
    return {x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1};
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanTextTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanTextTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 40;
    const int height = 24;
    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;

    // --- Case 1: solid-color text glyph geometry, translated by transform. ---
    {
        auto target = device.createRenderTarget(width, height);
        if (!target || !target->isValid()) {
            return 1;
        }
        DrawTextData d;
        // A 10x10 glyph quad at local origin, translated to (15, 7) by transform.
        d.vertices = quad(0.0f, 0.0f, 10.0f, 10.0f);
        d.color[0] = 0.0f;
        d.color[1] = 1.0f;
        d.color[2] = 0.0f;
        d.color[3] = 1.0f;
        d.transform = glm::translate(glm::mat4(1.0f), glm::vec3(15.0f, 7.0f, 0.0f));

        std::vector<std::unique_ptr<Command>> commands;
        commands.push_back(std::make_unique<DrawTextCommand>(d));
        if (!device.executeCommands(target, commands, request)) {
            std::cerr << "[VulkanTextTests] FAIL: solid text executeCommands returned false." << std::endl;
            return 1;
        }
        std::vector<unsigned char> px;
        if (!device.readPixelsRGBA(width, height, px)) {
            return 1;
        }
        // Inside the translated quad (e.g. 20,12) -> green; outside (2,2) -> clear.
        if (!near4(px, width, 20, 12, 0, 255, 0, 255, 4, "solid text inside")) return 1;
        if (!near4(px, width, 2, 2, 0, 0, 0, 0, 4, "solid text outside")) return 1;
        commands.clear();
        target.reset();
    }

    // --- Case 2: linear-gradient text (red@0 -> blue@1 across local x). ---
    {
        auto target = device.createRenderTarget(width, height);
        if (!target || !target->isValid()) {
            return 1;
        }
        DrawTextData d;
        d.vertices = quad(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        d.gradientType = DrawGradientType::Linear;
        d.gradientTileMode = DrawGradientTileMode::Clamp;
        d.gradientStart[0] = 0.0f;
        d.gradientStart[1] = 0.0f;
        d.gradientEnd[0] = static_cast<float>(width);
        d.gradientEnd[1] = 0.0f;
        d.gradientStopCount = 2;
        d.gradientStopPositions[0] = 0.0f;
        d.gradientStopPositions[1] = 1.0f;
        d.gradientStopColors[0] = 1.0f; // red
        d.gradientStopColors[3] = 1.0f;
        d.gradientStopColors[6] = 1.0f; // blue
        d.gradientStopColors[7] = 1.0f;

        std::vector<std::unique_ptr<Command>> commands;
        commands.push_back(std::make_unique<DrawTextCommand>(d));
        if (!device.executeCommands(target, commands, request)) {
            std::cerr << "[VulkanTextTests] FAIL: gradient text executeCommands returned false." << std::endl;
            return 1;
        }
        std::vector<unsigned char> px;
        if (!device.readPixelsRGBA(width, height, px)) {
            return 1;
        }
        // Left edge ~ red, right edge ~ blue, center ~ midpoint blend.
        const std::size_t left = (12u * width + 1u) * 4u;
        const std::size_t right = (12u * width + (width - 2u)) * 4u;
        if (px[left] < 200 || px[left + 2] > 60) {
            std::cerr << "[VulkanTextTests] FAIL: gradient left not red (" << (int)px[left] << ","
                      << (int)px[left + 1] << "," << (int)px[left + 2] << ")." << std::endl;
            return 1;
        }
        if (px[right + 2] < 200 || px[right] > 60) {
            std::cerr << "[VulkanTextTests] FAIL: gradient right not blue (" << (int)px[right] << ","
                      << (int)px[right + 1] << "," << (int)px[right + 2] << ")." << std::endl;
            return 1;
        }
        commands.clear();
        target.reset();
    }

    std::cout << "[VulkanTextTests] PASS: text geometry (solid + gradient) on \"" << device.selectedDeviceName()
              << "\"." << std::endl;
    device.finalizeBackend();
    return 0;
}
