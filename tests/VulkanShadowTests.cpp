// Gaussian drop-shadow translation: DrawShadowCommand renders a white silhouette
// into an offscreen coverage target, separable-Gaussian-blurs it, and composites
// the tinted blurred coverage. This test drives a shadow of an offset square and
// checks that a blurred, tinted footprint lands where expected while distant
// pixels stay clear. Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

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

int alphaAt(const std::vector<unsigned char> &px, int width, int x, int y)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    return px[idx + 3];
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanShadowTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanShadowTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 48;
    const int height = 48;
    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;

    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanShadowTests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    // A 16x16 square centered at (20,20); the shadow offset is baked into the
    // silhouette transform (translate +6,+6 -> footprint around (26,26)).
    auto square = [](float x0, float y0, float x1, float y1) {
        return std::vector<float>{x0, y0, x1, y0, x1, y1, x0, y0, x1, y1, x0, y1};
    };

    DrawShadowData shadow;
    shadow.canvasWidth = width;
    shadow.canvasHeight = height;
    shadow.blurRadius = 4.0f;
    shadow.color[0] = 1.0f; // red shadow tint
    shadow.color[1] = 0.0f;
    shadow.color[2] = 0.0f;
    shadow.color[3] = 1.0f;
    shadow.blendMode = DrawBlendMode::SrcOver;
    shadow.silhouette.points = square(12.0f, 12.0f, 28.0f, 28.0f);
    shadow.silhouette.color[0] = 1.0f;
    shadow.silhouette.color[1] = 1.0f;
    shadow.silhouette.color[2] = 1.0f;
    shadow.silhouette.color[3] = 1.0f;
    shadow.silhouette.drawMode = PathDrawMode::Fill;
    shadow.silhouette.transform = glm::translate(glm::mat4(1.0f), glm::vec3(6.0f, 6.0f, 0.0f));

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawShadowCommand>(shadow));
    if (!device.executeCommands(target, commands, request)) {
        std::cerr << "[VulkanShadowTests] FAIL: executeCommands returned false." << std::endl;
        return 1;
    }

    std::vector<unsigned char> px;
    if (!device.readPixelsRGBA(width, height, px)) {
        return 1;
    }

    // Center of the shadow footprint (~26,26): strongly covered and red-tinted.
    const std::size_t center = (26u * width + 26u) * 4u;
    if (px[center + 3] < 180) {
        std::cerr << "[VulkanShadowTests] FAIL: shadow center alpha too low (" << (int)px[center + 3] << ")."
                  << std::endl;
        return 1;
    }
    if (px[center + 0] < 150 || px[center + 1] > 40 || px[center + 2] > 40) {
        std::cerr << "[VulkanShadowTests] FAIL: shadow center not red-tinted (" << (int)px[center + 0] << ","
                  << (int)px[center + 1] << "," << (int)px[center + 2] << ")." << std::endl;
        return 1;
    }

    // Far corner (2,2) should be essentially clear.
    if (alphaAt(px, width, 2, 2) > 12) {
        std::cerr << "[VulkanShadowTests] FAIL: far corner not clear (" << alphaAt(px, width, 2, 2) << ")."
                  << std::endl;
        return 1;
    }

    // The blur should feather the edge: a band just outside the hard silhouette
    // (e.g. x=34, y=26, ~6px past the square's right edge at 28+6=34) should be a
    // partial (soft) coverage, less than the center but non-zero.
    const int edgeAlpha = alphaAt(px, width, 36, 26);
    if (edgeAlpha <= 3 || edgeAlpha >= px[center + 3]) {
        std::cerr << "[VulkanShadowTests] FAIL: expected soft feathered edge, got alpha " << edgeAlpha
                  << " (center " << (int)px[center + 3] << ")." << std::endl;
        return 1;
    }

    std::cout << "[VulkanShadowTests] PASS: Gaussian drop shadow on \"" << device.selectedDeviceName() << "\"."
              << std::endl;

    commands.clear();
    target.reset();

    // --- Bitmap / glyph-atlas silhouette shadow (imageSilhouette path). ---
    {
        auto target2 = device.createRenderTarget(width, height);
        if (!target2 || !target2->isValid()) {
            return 1;
        }
        // An opaque 2x2 white "glyph" texture; its alpha is the coverage.
        std::vector<unsigned char> glyph(2 * 2 * 4, 255);
        SharedImageResource glyphTex = device.createImageResourceRGBA(2, 2, glyph);
        if (!glyphTex || !glyphTex->isValid()) {
            std::cerr << "[VulkanShadowTests] FAIL: could not create glyph texture." << std::endl;
            return 1;
        }
        DrawShadowData bshadow;
        bshadow.canvasWidth = width;
        bshadow.canvasHeight = height;
        bshadow.blurRadius = 4.0f;
        bshadow.color[0] = 0.0f; // blue shadow tint
        bshadow.color[1] = 0.0f;
        bshadow.color[2] = 1.0f;
        bshadow.color[3] = 1.0f;
        bshadow.blendMode = DrawBlendMode::SrcOver;
        DrawImageData glyphQuad;
        glyphQuad.imageResource = glyphTex;
        glyphQuad.x = 18.0f;
        glyphQuad.y = 18.0f;
        glyphQuad.width = 12.0f;
        glyphQuad.height = 12.0f;
        glyphQuad.u0 = 0.0f;
        glyphQuad.v0 = 0.0f;
        glyphQuad.u1 = 1.0f;
        glyphQuad.v1 = 1.0f;
        glyphQuad.alpha = 1.0f;
        bshadow.imageSilhouette.push_back(glyphQuad);

        std::vector<std::unique_ptr<Command>> bcommands;
        bcommands.push_back(std::make_unique<DrawShadowCommand>(bshadow));
        if (!device.executeCommands(target2, bcommands, request)) {
            std::cerr << "[VulkanShadowTests] FAIL: bitmap shadow executeCommands returned false." << std::endl;
            return 1;
        }
        std::vector<unsigned char> bpx;
        if (!device.readPixelsRGBA(width, height, bpx)) {
            return 1;
        }
        // Center of the glyph footprint (~24,24): covered and blue-tinted.
        const std::size_t bcenter = (24u * width + 24u) * 4u;
        if (bpx[bcenter + 3] < 150 || bpx[bcenter + 2] < 120 || bpx[bcenter + 0] > 40) {
            std::cerr << "[VulkanShadowTests] FAIL: bitmap shadow center not blue (" << (int)bpx[bcenter + 0]
                      << "," << (int)bpx[bcenter + 1] << "," << (int)bpx[bcenter + 2] << ","
                      << (int)bpx[bcenter + 3] << ")." << std::endl;
            return 1;
        }
        if (alphaAt(bpx, width, 2, 2) > 12) {
            std::cerr << "[VulkanShadowTests] FAIL: bitmap shadow far corner not clear." << std::endl;
            return 1;
        }
        std::cout << "[VulkanShadowTests] PASS: bitmap/glyph-atlas shadow." << std::endl;
        bcommands.clear();
        glyphQuad.imageResource.reset();
        glyphTex.reset();
        target2.reset();
    }

    device.finalizeBackend();
    return 0;
}
