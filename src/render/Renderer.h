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

    size_t commandCount() const
    {
        return commands_.size();
    }

    std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t index)
    {
        std::vector<std::unique_ptr<Command>> taken;
        if (index >= commands_.size()) {
            return taken;
        }

        taken.reserve(commands_.size() - index);
        for (size_t i = index; i < commands_.size(); ++i) {
            taken.push_back(std::move(commands_[i]));
        }
        commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(index), commands_.end());
        return taken;
    }

    void appendCommands(std::vector<std::unique_ptr<Command>> &&commands)
    {
        for (auto &command : commands) {
            commands_.push_back(std::move(command));
        }
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