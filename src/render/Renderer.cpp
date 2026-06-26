// Renderer.cpp
#include "Renderer.h"

#include "command/DrawCommand.h"
#include "RenderDeviceFactory.h"

#include <cmath>
#include <glm/glm.hpp>

namespace {
constexpr float kMergeEpsilon = 0.001f;
} // namespace

Renderer::Renderer()
    : Renderer(RenderDeviceFactory::createBestAvailable())
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

bool Renderer::updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                       const unsigned char *pixels, bool regenerateMipmaps) const
{
    return device_ != nullptr
        && device_->updateImageResourceRGBA(imageResource, x, y, width, height, pixels, regenerateMipmaps);
}

SharedImageResource Renderer::wrapExternalImageResource(ImageResourceHandle handle) const
{
    return device_ == nullptr ? SharedImageResource() : device_->wrapExternalImageResource(handle);
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
    stats_.commandCount += commands_.size();
    auto executeCommand = [&](const std::unique_ptr<Command> &command) {
        command->execute(context_);
        ++stats_.drawCallCount;
    };

    // Merge consecutive compatible DrawPathCommands to reduce draw calls.
    // Two consecutive path commands are compatible when they share:
    // - same draw mode (fill/stroke)
    // - same transform matrix
    // - same blend mode
    // - same scissor state
    // - same clip mask fingerprint
    // - same cap style and stroke width (for strokes)
    // - uniform color (no per-vertex colors)

    std::size_t i = 0;
    while (i < commands_.size()) {
        if (commands_[i]->type() != Command::Type::Path) {
            executeCommand(commands_[i]);
            ++i;
            continue;
        }

        auto *pathCmd = static_cast<DrawPathCommand *>(commands_[i].get());
        if (pathCmd->data().hasVertexColors()) {
            executeCommand(commands_[i]);
            ++i;
            continue;
        }

        // Try to merge consecutive compatible DrawPathCommands.
        const auto &first = pathCmd->data();

        std::size_t j = i + 1;
        while (j < commands_.size()) {
            if (commands_[j]->type() != Command::Type::Path) {
                break;
            }

            auto *nextPathCmd = static_cast<DrawPathCommand *>(commands_[j].get());
            const auto &next = nextPathCmd->data();
            if (next.hasVertexColors()) {
                break;
            }
            if (next.drawMode != first.drawMode) {
                break;
            }
            if (next.capStyle != first.capStyle) {
                break;
            }
            if (std::abs(next.width - first.width) > kMergeEpsilon) {
                break;
            }
            if (next.blendMode != first.blendMode) {
                break;
            }
            if (next.transform != first.transform) {
                break;
            }
            if (next.scissor.enabled != first.scissor.enabled ||
                next.scissor.x != first.scissor.x ||
                next.scissor.y != first.scissor.y ||
                next.scissor.width != first.scissor.width ||
                next.scissor.height != first.scissor.height) {
                break;
            }
            if (next.clipMask.fingerprint != first.clipMask.fingerprint) {
                break;
            }
            if (std::abs(next.color[0] - first.color[0]) > kMergeEpsilon ||
                std::abs(next.color[1] - first.color[1]) > kMergeEpsilon ||
                std::abs(next.color[2] - first.color[2]) > kMergeEpsilon ||
                std::abs(next.color[3] - first.color[3]) > kMergeEpsilon) {
                break;
            }
            ++j;
        }

        if (j > i + 1) {
            // Pre-scan total point count for efficient reservation.
            std::size_t totalPoints = first.points.size();
            for (std::size_t m = i + 1; m < j; ++m) {
                totalPoints += static_cast<DrawPathCommand *>(commands_[m].get())->data().points.size();
            }

            DrawPathData merged = first;
            merged.points.reserve(totalPoints);

            for (std::size_t m = i + 1; m < j; ++m) {
                const auto &next = static_cast<DrawPathCommand *>(commands_[m].get())->data();
                merged.points.insert(merged.points.end(), next.points.begin(), next.points.end());
            }

            DrawPathCommand mergedCmd(merged);
            mergedCmd.execute(context_);
            ++stats_.drawCallCount;
            ++stats_.mergedBatchCount;
            i = j;
        } else {
            executeCommand(commands_[i]);
            ++i;
        }
    }
}
