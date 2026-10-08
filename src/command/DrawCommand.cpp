#include "command/DrawCommand.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <utility>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "command/DrawImage.h"
#include "command/DrawLines.h"
#include "command/DrawPath.h"
#include "command/DrawPathCommandPool.h"
#include "command/DrawPoints.h"
#include "command/DrawText.h"
#include "opengl/GaussianBlurProgram.h"
#include "render/GammaCorrect.h"
#include "render/RenderContext.h"

DrawPointsCommand::DrawPointsCommand(const DrawPointsData &data)
    : Command(Type::Points), data_(data)
{
}

void DrawPointsCommand::execute(RenderContext &context)
{
    context.applyClipState(data_.scissor, data_.clipMask);
    context.applyBlendMode(data_.blendMode);
    DrawPointsProgram::getInstance()->draw(context, data_);
}

DrawLinesCommand::DrawLinesCommand(const DrawLinesData &data)
    : Command(Type::Lines), data_(data)
{
}

void DrawLinesCommand::execute(RenderContext &context)
{
    context.applyClipState(data_.scissor, data_.clipMask);
    context.applyBlendMode(data_.blendMode);
    DrawLinesProgram::getInstance()->draw(context, data_);
}

DrawPathCommand::DrawPathCommand(const DrawPathData &data)
    : Command(Type::Path), data_(data)
{
}

DrawPathCommand::DrawPathCommand(DrawPathData &&data)
    : Command(Type::Path), data_(std::move(data))
{
}

void *DrawPathCommand::operator new(std::size_t size)
{
    if (size != sizeof(DrawPathCommand)) {
        return ::operator new(size);
    }
    return wsc::detail::allocateDrawPathCommand(size);
}

void DrawPathCommand::operator delete(void *memory) noexcept
{
    wsc::detail::releaseDrawPathCommand(memory);
}

void DrawPathCommand::execute(RenderContext &context)
{
    context.applyClipState(data_.scissor, data_.clipMask);
    context.applyBlendMode(data_.blendMode);
    DrawPathProgram::getInstance()->draw(context, data_);
}

DrawImageCommand::DrawImageCommand(const DrawImageData &data)
    : Command(Type::Image), data_(data)
{
}

void DrawImageCommand::execute(RenderContext &context)
{
    context.applyClipState(data_.scissor, data_.clipMask);
    if (!data_.clearTypeMask || !context.applyClearTypeBlendMode()) {
        context.applyBlendMode(data_.blendMode);
    }
    DrawImageProgram::getInstance()->draw(context, data_);
}

DrawImageBatchCommand::DrawImageBatchCommand(DrawImageBatchData data)
    : Command(Type::ImageBatch), data_(std::move(data))
{
}

void DrawImageBatchCommand::execute(RenderContext &context)
{
    for (const DrawImageBatchQuad &quad : data_.quads) {
        DrawImageData image;
        image.imageResource = data_.imageResource;
        image.x = quad.x;
        image.y = quad.y;
        image.width = quad.width;
        image.height = quad.height;
        image.u0 = quad.u0;
        image.v0 = quad.v0;
        image.u1 = quad.u1;
        image.v1 = quad.v1;
        image.tintColor[0] = data_.tintColor[0]
            * static_cast<float>(quad.packedTint & 0xffu) / 255.0f;
        image.tintColor[1] = data_.tintColor[1]
            * static_cast<float>((quad.packedTint >> 8u) & 0xffu)
                / 255.0f;
        image.tintColor[2] = data_.tintColor[2]
            * static_cast<float>((quad.packedTint >> 16u) & 0xffu)
                / 255.0f;
        image.tintColor[3] = data_.tintColor[3];
        image.alpha = data_.alpha
            * static_cast<float>((quad.packedTint >> 24u) & 0xffu)
                / 255.0f;
        image.transform = data_.transform;
        image.scissor = data_.scissor;
        image.blendMode = data_.blendMode;
        image.clipMask = data_.clipMask;
        DrawImageCommand(image).execute(context);
    }
}

DrawTextCommand::DrawTextCommand(const DrawTextData &data)
    : Command(Type::Text), data_(data)
{
}

void DrawTextCommand::execute(RenderContext &context)
{
    context.applyClipState(data_.scissor, data_.clipMask);
    context.applyBlendMode(data_.blendMode);
    DrawTextProgram::getInstance()->draw(context, data_);
}

DrawShadowCommand::DrawShadowCommand(const DrawShadowData &data)
    : Command(Type::Shadow), data_(data)
{
}

namespace {

bool shadowScissors(const DrawShadowData &data, int width, int height,
                    int blurRadius, glm::ivec4 &horizontal,
                    glm::ivec4 &vertical)
{
    float minX = static_cast<float>(width);
    float minY = static_cast<float>(height);
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool hasPoint = false;
    bool valid = true;
    const auto include = [&](const glm::mat4 &transform, float x, float y) {
        const glm::vec4 point = transform * glm::vec4(x, y, 0.0f, 1.0f);
        if (!std::isfinite(point.x) || !std::isfinite(point.y)
            || !std::isfinite(point.w) || std::abs(point.w - 1.0f) > 1e-5f) {
            valid = false;
            return;
        }
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
        hasPoint = true;
    };
    for (std::size_t i = 0; i + 1 < data.silhouette.points.size(); i += 2) {
        include(data.silhouette.transform,
                data.silhouette.points[i], data.silhouette.points[i + 1]);
    }
    for (const DrawImageData &image : data.imageSilhouette) {
        include(image.transform, image.x, image.y);
        include(image.transform, image.x + image.width, image.y);
        include(image.transform, image.x, image.y + image.height);
        include(image.transform, image.x + image.width,
                image.y + image.height);
    }
    const float safeCoordinate =
        static_cast<float>(std::numeric_limits<int>::max() / 4);
    if (!valid || !hasPoint || minX < -safeCoordinate || minY < -safeCoordinate
        || maxX > safeCoordinate || maxY > safeCoordinate) return false;
    const auto rect = [&](int xPad, int yPad) {
        const int left = std::clamp(
            static_cast<int>(std::floor(minX)) - xPad, 0, width);
        const int right = std::clamp(
            static_cast<int>(std::ceil(maxX)) + xPad, 0, width);
        const int top = std::clamp(
            static_cast<int>(std::floor(minY)) - yPad, 0, height);
        const int bottom = std::clamp(
            static_cast<int>(std::ceil(maxY)) + yPad, 0, height);
        return glm::ivec4(left, height - bottom, right - left, bottom - top);
    };
    horizontal = rect(blurRadius + 2, 2);
    vertical = rect(blurRadius + 2, blurRadius + 2);
    return horizontal.z > 0 && horizontal.w > 0
        && vertical.z > 0 && vertical.w > 0;
}

} // namespace

void DrawShadowCommand::execute(RenderContext &context)
{
    const int w = data_.canvasWidth;
    const int h = data_.canvasHeight;
    if (w <= 0 || h <= 0 || !(data_.blurRadius > 0.0f)) {
        return;
    }

    auto *blur = wsc::opengl::GaussianBlurProgram::getInstance();
    blur->initialize();
    if (!blur->ensureTargets(w, h)) {
        return;
    }

    // Save the framebuffer/viewport so later commands render into the frame.
    GLint previousFramebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    GLint previousViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    const wsc::render::GaussianKernel kernel =
        wsc::render::computeGaussianKernel(data_.blurRadius);
    glm::ivec4 horizontalScissor;
    glm::ivec4 verticalScissor;
    const bool boundedShadow = previousViewport[0] == 0
        && previousViewport[1] == 0
        && previousViewport[2] == w
        && previousViewport[3] == h
        && shadowScissors(data_, w, h, kernel.radius(),
                          horizontalScissor, verticalScissor);

    // 1. Render the shape silhouette (white, offset) into blur target A. The
    //    path program's projection uses the context size, which matches A.
    glBindFramebuffer(GL_FRAMEBUFFER, blur->framebuffer(0));
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!data_.imageSilhouette.empty()) {
        // Textured text (glyph atlas / bitmap): draw the glyph quads so their
        // sampled texture alpha accumulates as coverage. These go through the
        // image program, which manages its own blend/clip via the context, so
        // invalidate the cached GL state first (the raw calls above bypassed it).
        context.resetRenderState();
        for (const auto &imageData : data_.imageSilhouette) {
            DrawImageCommand(imageData).execute(context);
        }
    } else {
        context.resetRenderState();
        DrawPathProgram::getInstance()->draw(context, data_.silhouette);
    }

    // 2. Separable Gaussian blur: A -> B (horizontal) -> A (vertical).
    if (boundedShadow) {
        // The horizontal target is reused by later shadows. Clear its stale
        // texels before the vertical pass samples around the bounded region.
        glBindFramebuffer(GL_FRAMEBUFFER, blur->framebuffer(1));
        glDisable(GL_SCISSOR_TEST);
        glClear(GL_COLOR_BUFFER_BIT);
        blur->blurShadowPass(blur->texture(0), blur->framebuffer(1), w, h,
            glm::vec2(1.0f / static_cast<float>(w), 0.0f), kernel,
            horizontalScissor);
        blur->blurShadowPass(blur->texture(1), blur->framebuffer(0), w, h,
            glm::vec2(0.0f, 1.0f / static_cast<float>(h)), kernel,
            verticalScissor);
    } else {
        blur->blurPass(blur->texture(0), blur->framebuffer(1), w, h,
            glm::vec2(1.0f / static_cast<float>(w), 0.0f), kernel);
        blur->blurPass(blur->texture(1), blur->framebuffer(0), w, h,
            glm::vec2(0.0f, 1.0f / static_cast<float>(h)), kernel);
    }

    // 3. Restore the frame and composite the tinted blurred coverage. Blend and
    //    scissor are set with explicit GL calls (not the cached context) because
    //    the offscreen passes changed GL state directly, so the context cache is
    //    unreliable here.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                        GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if (boundedShadow) {
        int left = verticalScissor.x;
        int bottom = verticalScissor.y;
        int right = left + verticalScissor.z;
        int top = bottom + verticalScissor.w;
        if (data_.scissor.enabled) {
            left = std::max(left, data_.scissor.x);
            bottom = std::max(bottom, data_.scissor.y);
            right = std::min(right, data_.scissor.x + data_.scissor.width);
            top = std::min(top, data_.scissor.y + data_.scissor.height);
        }
        glEnable(GL_SCISSOR_TEST);
        glScissor(left, bottom,
                  std::max(0, right - left), std::max(0, top - bottom));
    } else if (data_.scissor.enabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(data_.scissor.x, data_.scissor.y, data_.scissor.width, data_.scissor.height);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    blur->composite(blur->texture(0),
                    glm::vec4(data_.color[0], data_.color[1], data_.color[2], data_.color[3]));
    glDisable(GL_SCISSOR_TEST);

    // Following commands re-apply their own state from a clean cache.
    context.resetRenderState();
}
