#pragma once

#include "command/DrawData.h"

class RenderContext;

// **********************************
// ***** Command class *****
// **********************************
class Command
{
public:
    enum class Type : std::uint8_t {
        Points,
        Lines,
        Path,
        Image,
        ImageBatch,
        Text,
        Shadow
    };

    virtual ~Command() = default;
    virtual void execute(RenderContext &context) = 0;

    Type type() const { return type_; }

protected:
    explicit Command(Type t) : type_(t) {}

private:
    Type type_;
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
    const DrawPointsData &data() const { return data_; }

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
    const DrawLinesData &data() const { return data_; }

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
    explicit DrawPathCommand(DrawPathData &&data);
    ~DrawPathCommand() override = default;

    void execute(RenderContext &context) override;

    const DrawPathData &data() const { return data_; }

private:
    DrawPathData data_;
};

class DrawImageCommand : public Command
{
public:
    explicit DrawImageCommand(const DrawImageData &data);
    ~DrawImageCommand() override = default;

    void execute(RenderContext &context) override;
    const DrawImageData &data() const { return data_; }

private:
    DrawImageData data_;
};

class DrawImageBatchCommand : public Command
{
public:
    explicit DrawImageBatchCommand(DrawImageBatchData data);
    ~DrawImageBatchCommand() override = default;

    void execute(RenderContext &context) override;
    const DrawImageBatchData &data() const { return data_; }

private:
    DrawImageBatchData data_;
};

class DrawTextCommand : public Command
{
public:
    explicit DrawTextCommand(const DrawTextData &data);
    ~DrawTextCommand() override = default;

    void execute(RenderContext &context) override;
    const DrawTextData &data() const { return data_; }

private:
    DrawTextData data_;
};

class DrawShadowCommand : public Command
{
public:
    explicit DrawShadowCommand(const DrawShadowData &data);
    ~DrawShadowCommand() override = default;

    void execute(RenderContext &context) override;
    const DrawShadowData &data() const { return data_; }

private:
    DrawShadowData data_;
};
