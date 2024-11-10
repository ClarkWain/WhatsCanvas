#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "render/RenderContext.h"
#include "command/DrawData.h"
#include "command/DrawPoints.h"
#include "command/DrawLines.h"

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
        DrawLinesProgram::getInstance()->draw(context, data_);
    }

private:
    DrawLinesData data_;
};