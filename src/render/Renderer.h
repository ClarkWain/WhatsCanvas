#pragma once

#include <vector>
#include <memory>

#include "RenderContext.h"
#include "command/DrawCommand.h"

class Renderer
{
public:
    static void initialize();
    static void finalize();

public:
    Renderer() = default;
    ~Renderer() = default;

    void setViewport(int width, int height)
    {
        context_.setSize(width, height);
    }

    void submit(std::unique_ptr<Command> &&command)
    {
        commands_.push_back(std::move(command));
    }

    void clear()
    {
        commands_.clear();
    }
    
    void flush()
    {
        for (const auto &command : commands_)
            command->execute(context_);
    }

private:
    std::vector<std::unique_ptr<Command>> commands_;
    RenderContext context_;
};