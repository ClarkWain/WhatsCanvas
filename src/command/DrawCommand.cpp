#include "command/DrawCommand.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "command/DrawImage.h"
#include "command/DrawLines.h"
#include "command/DrawPath.h"
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
    const wsc::render::GaussianKernel kernel = wsc::render::computeGaussianKernel(data_.blurRadius);
    blur->blurPass(blur->texture(0), blur->framebuffer(1), w, h,
                   glm::vec2(1.0f / static_cast<float>(w), 0.0f), kernel);
    blur->blurPass(blur->texture(1), blur->framebuffer(0), w, h,
                   glm::vec2(0.0f, 1.0f / static_cast<float>(h)), kernel);

    // 3. Restore the frame and composite the tinted blurred coverage. Blend and
    //    scissor are set with explicit GL calls (not the cached context) because
    //    the offscreen passes changed GL state directly, so the context cache is
    //    unreliable here.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (data_.scissor.enabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(data_.scissor.x, data_.scissor.y, data_.scissor.width, data_.scissor.height);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    blur->composite(blur->texture(0),
                    glm::vec4(data_.color[0], data_.color[1], data_.color[2], data_.color[3]));

    // Following commands re-apply their own state from a clean cache.
    context.resetRenderState();
}
