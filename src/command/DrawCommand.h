#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "render/RenderContext.h"
#include "command/DrawData.h"
#include "command/DrawProgram.h"

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

