#include "command/DrawCommand.h"

#include "command/DrawImage.h"
#include "command/DrawLines.h"
#include "command/DrawPath.h"
#include "command/DrawPoints.h"
#include "command/DrawText.h"
#include "render/RenderContext.h"

DrawPointsCommand::DrawPointsCommand(const DrawPointsData &data)
    : data_(data)
{
}

void DrawPointsCommand::execute(RenderContext &context)
{
    context.applyBlendMode(data_.blendMode);
    context.applyClipState(data_.scissor, data_.clipMask);
    DrawPointsProgram::getInstance()->draw(context, data_);
}

DrawLinesCommand::DrawLinesCommand(const DrawLinesData &data)
    : data_(data)
{
}

void DrawLinesCommand::execute(RenderContext &context)
{
    context.applyBlendMode(data_.blendMode);
    context.applyClipState(data_.scissor, data_.clipMask);
    DrawLinesProgram::getInstance()->draw(context, data_);
}

DrawPathCommand::DrawPathCommand(const DrawPathData &data)
    : data_(data)
{
}

void DrawPathCommand::execute(RenderContext &context)
{
    context.applyBlendMode(data_.blendMode);
    context.applyClipState(data_.scissor, data_.clipMask);
    DrawPathProgram::getInstance()->draw(context, data_);
}

DrawImageCommand::DrawImageCommand(const DrawImageData &data)
    : data_(data)
{
}

void DrawImageCommand::execute(RenderContext &context)
{
    context.applyBlendMode(data_.blendMode);
    context.applyClipState(data_.scissor, data_.clipMask);
    DrawImageProgram::getInstance()->draw(context, data_);
}

DrawTextCommand::DrawTextCommand(const DrawTextData &data)
    : data_(data)
{
}

void DrawTextCommand::execute(RenderContext &context)
{
    context.applyBlendMode(data_.blendMode);
    context.applyClipState(data_.scissor, data_.clipMask);
    DrawTextProgram::getInstance()->draw(context, data_);
}
