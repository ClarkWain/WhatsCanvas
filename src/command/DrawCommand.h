#pragma once

#include "command/DrawData.h"

class RenderContext;

// **********************************
// ***** Command class *****
// **********************************
class Command
{
public:
    virtual ~Command() = default;
    virtual void execute(RenderContext &context) = 0;
};

// **********************************
// ***** DrawPointsCommand class *****
// **********************************
class DrawPointsCommand : public Command
{
public:
    explicit DrawPointsCommand(const DrawPointsData &data);
    ~DrawPointsCommand() override = default;

    void execute(RenderContext &context) override;

private:
    DrawPointsData data_;
};

// **********************************
// ***** DrawLinesCommand class *****
// **********************************
class DrawLinesCommand : public Command
{
public:
    explicit DrawLinesCommand(const DrawLinesData &data);
    ~DrawLinesCommand() override = default;

    void execute(RenderContext &context) override;

private:
    DrawLinesData data_;
};

// **********************************
// ***** DrawPathCommand class *****
// **********************************
class DrawPathCommand : public Command
{
public:
    explicit DrawPathCommand(const DrawPathData &data);
    ~DrawPathCommand() override = default;

    void execute(RenderContext &context) override;

private:
    DrawPathData data_;
};

class DrawImageCommand : public Command
{
public:
    explicit DrawImageCommand(const DrawImageData &data);
    ~DrawImageCommand() override = default;

    void execute(RenderContext &context) override;

private:
    DrawImageData data_;
};

class DrawTextCommand : public Command
{
public:
    explicit DrawTextCommand(const DrawTextData &data);
    ~DrawTextCommand() override = default;

    void execute(RenderContext &context) override;

private:
    DrawTextData data_;
};
