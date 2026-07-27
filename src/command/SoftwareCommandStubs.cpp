// Software-only command definitions.
//
// In the GPU-free WhatsCanvasSoftware build the draw commands are pure data
// carriers: SoftwareRenderer consumes them through Command::type() and each
// command's data() accessor and never invokes the GPU execute() path. The
// regular build compiles the GL-backed constructors and execute() methods
// (plus the DrawXxxProgram shader classes) from DrawCommand.cpp / DrawXxx.cpp;
// those translation units pull in glad and the OpenGL headers, so they are not
// part of the software library. This file provides the trivial, dependency-free
// constructors and no-op execute() overrides so the command classes remain
// concrete and instantiable without any OpenGL symbols.

#include "command/DrawCommand.h"

#include <utility>

DrawPointsCommand::DrawPointsCommand(const DrawPointsData &data)
    : Command(Type::Points), data_(data)
{
}
void DrawPointsCommand::execute(RenderContext &) {}

DrawLinesCommand::DrawLinesCommand(const DrawLinesData &data)
    : Command(Type::Lines), data_(data)
{
}
void DrawLinesCommand::execute(RenderContext &) {}

DrawPathCommand::DrawPathCommand(const DrawPathData &data)
    : Command(Type::Path), data_(data)
{
}
DrawPathCommand::DrawPathCommand(DrawPathData &&data)
    : Command(Type::Path), data_(std::move(data))
{
}
void DrawPathCommand::execute(RenderContext &) {}

DrawImageCommand::DrawImageCommand(const DrawImageData &data)
    : Command(Type::Image), data_(data)
{
}
void DrawImageCommand::execute(RenderContext &) {}

DrawImageBatchCommand::DrawImageBatchCommand(DrawImageBatchData data)
    : Command(Type::ImageBatch), data_(std::move(data))
{
}
void DrawImageBatchCommand::execute(RenderContext &) {}

DrawTextCommand::DrawTextCommand(const DrawTextData &data)
    : Command(Type::Text), data_(data)
{
}
void DrawTextCommand::execute(RenderContext &) {}

DrawShadowCommand::DrawShadowCommand(const DrawShadowData &data)
    : Command(Type::Shadow), data_(data)
{
}
void DrawShadowCommand::execute(RenderContext &) {}
