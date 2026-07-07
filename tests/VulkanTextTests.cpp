// Text command translation: vector triangle text and glyph-atlas textured text.
// Glyph-atlas text reaches Vulkan as DrawImageCommand quads, matching the Canvas
// text path after shaping/raster/atlas upload.
// Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "command/DrawCommand.h"
#include "command/DrawData.h"
#include "canvas/Paint.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/vulkan/VulkanRenderDevice.h"
#include "text/BasicTextBackend.h"
#include "text/ITextBackend.h"

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

std::vector<unsigned char> makeAtlasRgba(const wsc::text::TextRenderResult &rendered)
{
    const std::size_t atlasSize =
        static_cast<std::size_t>(rendered.atlasWidth) * static_cast<std::size_t>(rendered.atlasHeight);
    if (rendered.atlasPixelFormat == wsc::text::GlyphAtlasPixelFormat::RGBA
        && rendered.atlasRgbaPixels.size() >= atlasSize * 4u) {
        return rendered.atlasRgbaPixels;
    }
    std::vector<unsigned char> rgba(atlasSize * 4u, 255u);
    for (std::size_t i = 0; i < atlasSize && i < rendered.atlasAlphaPixels.size(); ++i) {
        rgba[i * 4u + 0u] = 255u;
        rgba[i * 4u + 1u] = 255u;
        rgba[i * 4u + 2u] = 255u;
        rgba[i * 4u + 3u] = rendered.atlasAlphaPixels[i];
    }
    return rgba;
}

std::vector<unsigned char> makeAtlasRgbaRect(const wsc::text::TextRenderResult &rendered,
                                             const wsc::text::TextRenderResult::GlyphAtlasDirtyRect &rect)
{
    const std::vector<unsigned char> rgba = makeAtlasRgba(rendered);
    std::vector<unsigned char> subrect(static_cast<std::size_t>(rect.width) * static_cast<std::size_t>(rect.height) * 4u);
    for (int row = 0; row < rect.height; ++row) {
        const std::size_t src = (static_cast<std::size_t>(rect.y + row)
                                * static_cast<std::size_t>(rendered.atlasWidth)
                                + static_cast<std::size_t>(rect.x)) * 4u;
        const std::size_t dst = static_cast<std::size_t>(row) * static_cast<std::size_t>(rect.width) * 4u;
        std::copy(rgba.begin() + static_cast<std::ptrdiff_t>(src),
                  rgba.begin() + static_cast<std::ptrdiff_t>(src + static_cast<std::size_t>(rect.width) * 4u),
                  subrect.begin() + static_cast<std::ptrdiff_t>(dst));
    }
    return subrect;
}

bool hasVisiblePixel(const std::vector<unsigned char> &px)
{
    for (std::size_t i = 0; i + 3u < px.size(); i += 4u) {
        if (px[i + 3u] > 0u && (px[i] > 0u || px[i + 1u] > 0u || px[i + 2u] > 0u)) {
            return true;
        }
    }
    return false;
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

    // --- Case 3: real glyph-atlas text as textured image quads. ---
    {
        std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createBasicTextBackend();
        Paint paint;
        paint.setTextSize(18.0f);
        const wsc::text::TextRenderResult rendered = backend->renderText("Atlas", 4.0f, 18.0f, paint);
        if (rendered.kind != wsc::text::TextRenderKind::GlyphAtlas || rendered.glyphAtlasQuads.empty()
            || rendered.atlasWidth <= 0 || rendered.atlasHeight <= 0 || rendered.atlasAlphaPixels.empty()) {
            std::cout << "[VulkanTextTests] SKIP: system glyph-atlas text backend unavailable." << std::endl;
        } else {
            auto target = device.createRenderTarget(width, height);
            if (!target || !target->isValid()) {
                return 1;
            }
            const std::vector<unsigned char> atlasPixels = makeAtlasRgba(rendered);
            SharedImageResource atlas =
                device.createImageResourceRGBA(rendered.atlasWidth, rendered.atlasHeight, atlasPixels);
            if (!atlas || !atlas->isValid()) {
                std::cerr << "[VulkanTextTests] FAIL: could not create glyph atlas texture." << std::endl;
                return 1;
            }

            std::vector<std::unique_ptr<Command>> commands;
            for (const auto &quad : rendered.glyphAtlasQuads) {
                DrawImageData d;
                d.imageResource = atlas;
                d.x = quad.x;
                d.y = quad.y;
                d.width = quad.width;
                d.height = quad.height;
                d.u0 = quad.u0;
                d.v0 = quad.v0;
                d.u1 = quad.u1;
                d.v1 = quad.v1;
                d.tintColor[0] = 0.95f;
                d.tintColor[1] = 0.85f;
                d.tintColor[2] = 0.20f;
                d.tintColor[3] = 1.0f;
                d.alpha = 1.0f;
                d.sampling = DrawImageSampling::Linear;
                d.tileMode = DrawImageTileMode::Clamp;
                commands.push_back(std::make_unique<DrawImageCommand>(d));
            }
            if (!device.executeCommands(target, commands, request)) {
                std::cerr << "[VulkanTextTests] FAIL: glyph atlas executeCommands returned false." << std::endl;
                return 1;
            }
            std::vector<unsigned char> px;
            if (!device.readPixelsRGBA(width, height, px)) {
                return 1;
            }
            if (!hasVisiblePixel(px)) {
                std::cerr << "[VulkanTextTests] FAIL: glyph atlas text produced no visible pixels." << std::endl;
                return 1;
            }
            commands.clear();

            const wsc::text::TextRenderResult updated = backend->renderText("Atlas W", 4.0f, 18.0f, paint);
            if (updated.kind != wsc::text::TextRenderKind::GlyphAtlas || updated.glyphAtlasQuads.empty()
                || updated.atlasWidth <= 0 || updated.atlasHeight <= 0 || updated.atlasDirtyRects.empty()) {
                std::cerr << "[VulkanTextTests] FAIL: second glyph-atlas render did not report dirty rects."
                          << std::endl;
                return 1;
            }
            if (updated.atlasWidth != rendered.atlasWidth || updated.atlasHeight != rendered.atlasHeight) {
                const std::vector<unsigned char> updatedPixels = makeAtlasRgba(updated);
                atlas = device.createImageResourceRGBA(updated.atlasWidth, updated.atlasHeight, updatedPixels);
            } else {
                for (const auto &rect : updated.atlasDirtyRects) {
                    const std::vector<unsigned char> rectPixels = makeAtlasRgbaRect(updated, rect);
                    if (!device.updateImageResourceRGBA(atlas, rect.x, rect.y, rect.width, rect.height,
                                                        rectPixels.data(), false)) {
                        std::cerr << "[VulkanTextTests] FAIL: glyph atlas dirty rect update failed." << std::endl;
                        return 1;
                    }
                }
            }
            if (!atlas || !atlas->isValid()) {
                std::cerr << "[VulkanTextTests] FAIL: updated glyph atlas texture is invalid." << std::endl;
                return 1;
            }

            auto dirtyTarget = device.createRenderTarget(width, height);
            if (!dirtyTarget || !dirtyTarget->isValid()) {
                return 1;
            }
            std::vector<std::unique_ptr<Command>> dirtyCommands;
            for (const auto &quad : updated.glyphAtlasQuads) {
                DrawImageData d;
                d.imageResource = atlas;
                d.x = quad.x;
                d.y = quad.y;
                d.width = quad.width;
                d.height = quad.height;
                d.u0 = quad.u0;
                d.v0 = quad.v0;
                d.u1 = quad.u1;
                d.v1 = quad.v1;
                d.tintColor[0] = 0.20f;
                d.tintColor[1] = 0.95f;
                d.tintColor[2] = 0.55f;
                d.tintColor[3] = 1.0f;
                d.alpha = 1.0f;
                d.sampling = DrawImageSampling::Linear;
                d.tileMode = DrawImageTileMode::Clamp;
                dirtyCommands.push_back(std::make_unique<DrawImageCommand>(d));
            }
            if (!device.executeCommands(dirtyTarget, dirtyCommands, request)) {
                std::cerr << "[VulkanTextTests] FAIL: dirty glyph atlas executeCommands returned false." << std::endl;
                return 1;
            }
            if (!device.readPixelsRGBA(width, height, px) || !hasVisiblePixel(px)) {
                std::cerr << "[VulkanTextTests] FAIL: dirty glyph atlas text produced no visible pixels."
                          << std::endl;
                return 1;
            }
            dirtyCommands.clear();
            dirtyTarget.reset();
            atlas.reset();
            target.reset();
        }
    }

    std::cout << "[VulkanTextTests] PASS: text geometry and glyph atlas path on \"" << device.selectedDeviceName()
              << "\"." << std::endl;
    device.finalizeBackend();
    return 0;
}
