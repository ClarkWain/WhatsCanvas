// Renderer.cpp
#include "Renderer.h"
#include <glad/glad.h>
#include <gtc/type_ptr.hpp>
#include "command/DrawCommand.h"

void Renderer::initialize()
{
    DrawPointsProgram::getInstance()->initialize();
    DrawLinesProgram::getInstance()->initialize();
}

void Renderer::finalize()
{
    DrawPointsProgram::getInstance()->release();
    DrawLinesProgram::getInstance()->release();
}
