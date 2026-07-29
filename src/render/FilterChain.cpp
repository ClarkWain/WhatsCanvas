#include "IRenderer.h"

#include <glm/glm.hpp>

#include "command/DrawCommand.h"
#include "command/DrawData.h"

SharedImageResource IRenderer::filterImageResource(
    const SharedImageResource &source,
    int width, int height,
    const wsc::ImageFilterChain &filters,
    FilterExecutionStats *executionStats) const
{
    if (executionStats != nullptr) {
        *executionStats = {};
    }
    if (!source || !source->isValid() || width <= 0 || height <= 0
        || !filters.isValid()) {
        return {};
    }

    SharedImageResource current = source;
    FilterExecutionStats total;
    for (std::size_t index = 0; index < filters.size(); ++index) {
        const wsc::ImageFilterChain::Node &node = filters[index];
        if (node.type == wsc::ImageFilterChain::NodeType::ImageFilter) {
            FilterExecutionStats pass;
            current = filterImageResource(
                current, width, height, node.imageFilter, &pass);
            if (!current || !current->isValid()) {
                return {};
            }
            total.passCount += pass.passCount;
            total.pixelPassCount += pass.pixelPassCount;
            total.downsampled = total.downsampled || pass.downsampled;
            continue;
        }

        DrawImageData data;
        data.imageResource = current;
        data.x = node.type == wsc::ImageFilterChain::NodeType::Offset
            ? node.offsetX : 0.0f;
        data.y = node.type == wsc::ImageFilterChain::NodeType::Offset
            ? node.offsetY : 0.0f;
        data.width = static_cast<float>(width);
        data.height = static_cast<float>(height);
        data.u0 = 0.0f;
        data.u1 = 1.0f;
        const bool flip =
            current->origin() == ImageOrigin::BottomLeft;
        data.v0 = flip ? 1.0f : 0.0f;
        data.v1 = flip ? 0.0f : 1.0f;
        data.sampling = DrawImageSampling::Linear;
        data.tileMode = DrawImageTileMode::Clamp;
        data.blendMode = DrawBlendMode::Src;
        data.transform = glm::mat4(1.0f);
        if (node.type
            == wsc::ImageFilterChain::NodeType::ColorMatrix) {
            data.hasColorMatrix = true;
            for (std::size_t row = 0; row < 4u; ++row) {
                for (std::size_t column = 0; column < 4u; ++column) {
                    data.colorMatrix[column * 4u + row] =
                        node.colorMatrix[row * 5u + column];
                }
                data.colorMatrixOffset[row] =
                    node.colorMatrix[row * 5u + 4u];
            }
        }

        std::vector<std::unique_ptr<Command>> commands;
        commands.push_back(
            std::make_unique<DrawImageCommand>(std::move(data)));
        OffscreenRenderRequest request;
        request.canvasWidth = width;
        request.canvasHeight = height;
        request.targetWidth = width;
        request.targetHeight = height;
        current = renderCommandsToImageResource(commands, request);
        if (!current || !current->isValid()) {
            return {};
        }
        ++total.passCount;
        total.pixelPassCount +=
            static_cast<std::size_t>(width)
            * static_cast<std::size_t>(height);
        recordGenericFilterPass(width, height);
    }

    if (executionStats != nullptr) {
        *executionStats = total;
    }
    return current;
}
