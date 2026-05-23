// Renderer.cpp
#include "Renderer.h"
#include <glad/glad.h>
#include <gtc/type_ptr.hpp>
#include "command/DrawCommand.h"

void Renderer::initialize()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    DrawPointsProgram::getInstance()->initialize();
    DrawLinesProgram::getInstance()->initialize();
    DrawPathProgram::getInstance()->initialize();
    DrawImageProgram::getInstance()->initialize();
    DrawTextProgram::getInstance()->initialize();
}

void Renderer::finalize()
{
    DrawPointsProgram::getInstance()->release();
    DrawLinesProgram::getInstance()->release();
    DrawPathProgram::getInstance()->release();
    DrawImageProgram::getInstance()->release();
    DrawTextProgram::getInstance()->release();
}
