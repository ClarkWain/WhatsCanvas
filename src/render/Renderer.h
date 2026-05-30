#pragma once

#include <vector>
#include <memory>

#include "IRenderDevice.h"
#include "IRenderer.h"
#include "RenderContext.h"

class Renderer : public IRenderer
{
public:
    static void initialize();
    static void finalize();

public:
    Renderer();
    explicit Renderer(std::unique_ptr<IRenderDevice> device);
    ~Renderer() override;

    void initializeBackend() override;
    void finalizeBackend() override;

    void setViewport(int width, int height) override
    {
        context_.setSize(width, height);
    }

    void submit(std::unique_ptr<Command> &&command) override
    {
        commands_.push_back(std::move(command));
    }

    size_t commandCount() const override
    {
        return commands_.size();
    }

    std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t index) override
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

    void appendCommands(std::vector<std::unique_ptr<Command>> &&commands) override
    {
        for (auto &command : commands) {
            commands_.push_back(std::move(command));
        }
    }

    bool readPixelsRGBA(std::vector<unsigned char> &pixels) const override;
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const override;
    SharedImageResource createImageResourceRGBA(int width, int height, const std::vector<unsigned char> &pixels) const override;
    SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                         const unsigned char *pixels, bool generateMipmaps) const override;
    SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                      const OffscreenRenderRequest &request) const override;
    void resetRenderState() override;

    void clear() override
    {
        commands_.clear();
    }
    
    void flush() override
    {
        for (const auto &command : commands_)
            command->execute(context_);
    }

private:
    std::vector<std::unique_ptr<Command>> commands_;
    std::unique_ptr<IRenderDevice> device_;
    RenderContext context_;
    bool backendInitialized_ = false;
};