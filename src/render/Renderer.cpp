// Renderer.cpp
#include "Renderer.h"

#include "command/DrawCommand.h"
#include "OpenGLRenderDevice.h"

Renderer::Renderer()
    : Renderer(std::make_unique<OpenGLRenderDevice>())
{
}

Renderer::Renderer(std::unique_ptr<IRenderDevice> device)
    : device_(std::move(device))
{
}

void Renderer::initialize()
{
}

void Renderer::finalize()
{
}

Renderer::~Renderer()
{
    finalizeBackend();
}

void Renderer::initializeBackend()
{
    if (backendInitialized_ || device_ == nullptr) {
        return;
    }

    device_->initializeBackend();
    backendInitialized_ = true;
}

void Renderer::finalizeBackend()
{
    if (!backendInitialized_ || device_ == nullptr) {
        return;
    }

    device_->finalizeBackend();
    backendInitialized_ = false;
}

void Renderer::setViewport(int width, int height)
{
    context_.setSize(width, height);
}

void Renderer::submit(std::unique_ptr<Command> &&command)
{
    commands_.push_back(std::move(command));
}

size_t Renderer::commandCount() const
{
    return commands_.size();
}

std::vector<std::unique_ptr<Command>> Renderer::takeCommandsFrom(size_t index)
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

void Renderer::appendCommands(std::vector<std::unique_ptr<Command>> &&commands)
{
    for (auto &command : commands) {
        commands_.push_back(std::move(command));
    }
}

bool Renderer::readPixelsRGBA(std::vector<unsigned char> &pixels) const
{
    if (device_ == nullptr) {
        pixels.clear();
        return false;
    }

    return device_->readPixelsRGBA(context_.getWidth(), context_.getHeight(), pixels);
}

SharedClipMaskResource Renderer::createClipMaskResource(const ClipMaskPath &maskPath) const
{
    return device_ == nullptr ? SharedClipMaskResource() : device_->createClipMaskResource(maskPath);
}

SharedImageResource Renderer::createImageResourceRGBA(int width, int height, const std::vector<unsigned char> &pixels) const
{
    return device_ == nullptr ? SharedImageResource() : device_->createImageResourceRGBA(width, height, pixels);
}

SharedImageResource Renderer::createImageResourceFromImageData(int width, int height, int channels,
                                                               const unsigned char *pixels, bool generateMipmaps) const
{
    return device_ == nullptr
        ? SharedImageResource()
        : device_->createImageResourceFromImageData(width, height, channels, pixels, generateMipmaps);
}

SharedImageResource Renderer::renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                            const OffscreenRenderRequest &request) const
{
    return device_ == nullptr ? SharedImageResource() : device_->renderCommandsToImageResource(commands, request);
}

void Renderer::resetRenderState()
{
    context_.resetRenderState();
}

void Renderer::clear()
{
    commands_.clear();
}

void Renderer::flush()
{
    for (const auto &command : commands_) {
        command->execute(context_);
    }
}
