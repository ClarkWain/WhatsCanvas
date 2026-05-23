#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "render/RenderContext.h"
#include "command/DrawData.h"
#include "command/DrawPoints.h"
#include "command/DrawLines.h"
#include "command/DrawPath.h"
#include "command/DrawImage.h"
#include "command/DrawText.h"

inline void applyScissorState(RenderContext &context, const ScissorState &scissor)
{
    if (scissor.enabled) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(scissor.x + context.getScissorOffsetX(),
                  scissor.y + context.getScissorOffsetY(),
                  scissor.width,
                  scissor.height);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

inline void applyBlendMode(DrawBlendMode mode)
{
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);

    switch (mode) {
    case DrawBlendMode::SrcOver:
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case DrawBlendMode::Src:
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
        break;
    case DrawBlendMode::Dst:
        glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ZERO, GL_ONE);
        break;
    case DrawBlendMode::Clear:
        glBlendFuncSeparate(GL_ZERO, GL_ZERO, GL_ZERO, GL_ZERO);
        break;
    case DrawBlendMode::SrcIn:
        glBlendFuncSeparate(GL_DST_ALPHA, GL_ZERO, GL_DST_ALPHA, GL_ZERO);
        break;
    case DrawBlendMode::DstIn:
        glBlendFuncSeparate(GL_ZERO, GL_SRC_ALPHA, GL_ZERO, GL_SRC_ALPHA);
        break;
    case DrawBlendMode::SrcOut:
        glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_ZERO, GL_ONE_MINUS_DST_ALPHA, GL_ZERO);
        break;
    case DrawBlendMode::DstOut:
        glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case DrawBlendMode::SrcAtop:
        glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case DrawBlendMode::DstAtop:
        glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA);
        break;
    case DrawBlendMode::Xor:
        glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case DrawBlendMode::Add:
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
        break;
    case DrawBlendMode::Multiply:
        glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_DST_ALPHA, GL_ZERO);
        break;
    case DrawBlendMode::Screen:
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

// **********************************
// ***** Command 类 *****
// **********************************
class Command
{
public:
    virtual void execute(RenderContext &context) = 0;
};

// **********************************
// ***** DrawPointCommand 类 *****
// **********************************
class DrawPointsCommand : public Command
{
public:
    DrawPointsCommand(const DrawPointsData &data) : data_(data) {};

    ~DrawPointsCommand() = default;

    void execute(RenderContext &context) override
    {
        applyBlendMode(data_.blendMode);
        applyScissorState(context, data_.scissor);
        DrawPointsProgram::getInstance()->draw(context, data_);
    }

private:
    DrawPointsData data_;
};

// **********************************
// ***** DrawLinesCommand 类 *****
// **********************************
class DrawLinesCommand : public Command
{
public:
    DrawLinesCommand(const DrawLinesData &data) : data_(data) {};

    ~DrawLinesCommand() = default;

    void execute(RenderContext &context) override
    {
        applyBlendMode(data_.blendMode);
        applyScissorState(context, data_.scissor);
        DrawLinesProgram::getInstance()->draw(context, data_);
    }

private:
    DrawLinesData data_;
};

// **********************************
// ***** DrawPathCommand 类 *****
// **********************************
class DrawPathCommand : public Command
{
public:
    DrawPathCommand(const DrawPathData &data) : data_(data) {};

    ~DrawPathCommand() = default;

    void execute(RenderContext &context) override
    {
        applyBlendMode(data_.blendMode);
        applyScissorState(context, data_.scissor);
        DrawPathProgram::getInstance()->draw(context, data_);
    }

private:
    DrawPathData data_;
};

class DrawImageCommand : public Command
{
public:
    DrawImageCommand(const DrawImageData &data) : data_(data) {};

    ~DrawImageCommand()
    {
        if (data_.ownsTexture && data_.textureID != 0) {
            glDeleteTextures(1, &data_.textureID);
            data_.textureID = 0;
        }
    }

    void execute(RenderContext &context) override
    {
        applyBlendMode(data_.blendMode);
        applyScissorState(context, data_.scissor);
        DrawImageProgram::getInstance()->draw(context, data_);
    }

private:
    DrawImageData data_;
};

class DrawTextCommand : public Command
{
public:
    DrawTextCommand(const DrawTextData &data) : data_(data) {};

    ~DrawTextCommand() = default;

    void execute(RenderContext &context) override
    {
        applyBlendMode(data_.blendMode);
        applyScissorState(context, data_.scissor);
        DrawTextProgram::getInstance()->draw(context, data_);
    }

private:
    DrawTextData data_;
};