#pragma once

#include "render/RenderContext.h"
#include "command/DrawData.h"
#include "command/DrawPoints.h"
#include "command/DrawLines.h"
#include "command/DrawPath.h"
#include "command/DrawImage.h"
#include "command/DrawText.h"

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
        context.applyBlendMode(data_.blendMode);
        context.applyClipState(data_.scissor, data_.clipMask);
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
        context.applyBlendMode(data_.blendMode);
        context.applyClipState(data_.scissor, data_.clipMask);
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
        context.applyBlendMode(data_.blendMode);
        context.applyClipState(data_.scissor, data_.clipMask);
        DrawPathProgram::getInstance()->draw(context, data_);
    }

private:
    DrawPathData data_;
};

class DrawImageCommand : public Command
{
public:
    DrawImageCommand(const DrawImageData &data) : data_(data) {};

    ~DrawImageCommand() = default;

    void execute(RenderContext &context) override
    {
        context.applyBlendMode(data_.blendMode);
        context.applyClipState(data_.scissor, data_.clipMask);
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
        context.applyBlendMode(data_.blendMode);
        context.applyClipState(data_.scissor, data_.clipMask);
        DrawTextProgram::getInstance()->draw(context, data_);
    }

private:
    DrawTextData data_;
};