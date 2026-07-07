// Renderer.cpp
#include "Renderer.h"

#include "command/DrawCommand.h"
#include "RenderDeviceFactory.h"
#include "IRenderTarget.h"
#include "SpriteBatch.h"

#include <cmath>
#include <iostream>
#include <glm/glm.hpp>

#include "render/GammaCorrect.h"
#include "render/PathMerge.h"

namespace {
constexpr float kMergeEpsilon = 0.001f;

bool nearlyEqual(float lhs, float rhs)
{
    return std::abs(lhs - rhs) <= kMergeEpsilon;
}

bool isSpriteBatchCompatible(const DrawImageData &data, const SharedImageResource &texture, DrawBlendMode blendMode)
{
    return data.imageResource == texture
        && data.imageResource
        && data.imageResource->isValid()
        && !data.hasColorMatrix
        && !data.hasShaderGradient()
        && data.sampling == DrawImageSampling::Linear
        && data.tileMode == DrawImageTileMode::Clamp
        && !data.scissor.enabled
        && !data.clipMask.hasPaths()
        && data.blendMode == blendMode;
}
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

    // Release device-owned render targets before the backend (and its GPU
    // device) is torn down, so their handles are not destroyed against a dead
    // device.
    mainTarget_.reset();
    mainTargetWidth_ = 0;
    mainTargetHeight_ = 0;

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

RenderResourceStats Renderer::resourceStats() const
{
    return device_ == nullptr ? RenderResourceStats() : device_->resourceStats();
}

SharedImageResource Renderer::renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                            const OffscreenRenderRequest &request) const
{
    if (device_ == nullptr) {
        return {};
    }

    stats_.commandCount += commands.size();
    SharedImageResource resource = device_->renderCommandsToImageResource(commands, request);
    if (resource && resource->isValid()) {
        stats_.drawCallCount += commands.size();
        ++stats_.renderTargetSwitches;
    }
    return resource;
}

void Renderer::resetRenderState()
{
    // No GL context to reset for devices that render through executeCommands().
    if (device_ != nullptr && device_->usesDeviceCommandExecution()) {
        return;
    }
    context_.resetRenderState();
}

void Renderer::clear()
{
    commands_.clear();
}

void Renderer::flush()
{
    stats_.commandCount += commands_.size();

    // Devices such as Vulkan render the recorded command stream into a device
    // render target rather than executing each command against a GL context.
    // Never fall through to the GL execute path for these backends.
    if (device_ != nullptr && device_->usesDeviceCommandExecution()) {
        if (!flushViaDeviceCommands()) {
            std::cerr << "[Renderer] Device command execution failed; frame not rendered." << std::endl;
        }
        return;
    }

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
        if (commands_[i]->type() == Command::Type::Image) {
            auto *imageCmd = static_cast<DrawImageCommand *>(commands_[i].get());
            const auto &first = imageCmd->data();
            if (isSpriteBatchCompatible(first, first.imageResource, first.blendMode)) {
                std::size_t j = i + 1;
                while (j < commands_.size() && commands_[j]->type() == Command::Type::Image) {
                    const auto &next = static_cast<DrawImageCommand *>(commands_[j].get())->data();
                    if (!isSpriteBatchCompatible(next, first.imageResource, first.blendMode)) {
                        break;
                    }
                    ++j;
                }

                if (j > i + 1) {
                    SpriteBatch batch;
                    batch.setTexture(first.imageResource);
                    for (std::size_t m = i; m < j; ++m) {
                        const auto &data = static_cast<DrawImageCommand *>(commands_[m].get())->data();
                        float tintColor[4] = {
                            data.tintColor[0],
                            data.tintColor[1],
                            data.tintColor[2],
                            data.tintColor[3] * data.alpha
                        };
                        GammaCorrect::srgbToLinear4(tintColor);
                        batch.add(data.x, data.y, data.width, data.height,
                                  data.u0, data.v0, data.u1, data.v1,
                                  tintColor[0], tintColor[1], tintColor[2], tintColor[3],
                                  data.transform);
                    }
                    batch.flush(context_, first.blendMode);
                    ++stats_.drawCallCount;
                    ++stats_.mergedBatchCount;
                    i = j;
                    continue;
                }
            }
        }

        if (commands_[i]->type() != Command::Type::Path) {
            executeCommand(commands_[i]);
            ++i;
            continue;
        }

        auto *pathCmd = static_cast<DrawPathCommand *>(commands_[i].get());
        if (pathCmd->data().hasVertexColors() || pathCmd->data().hasShaderGradient()) {
            // Per-vertex colours and shader gradients carry per-shape state that
            // cannot be shared across a merged draw call, so render individually.
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
            if (!wsc::render::canMergePathData(first, next)) {
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
            if (first.hasCoverage()) {
                merged.coverage.reserve(totalPoints / 2);
            }

            for (std::size_t m = i + 1; m < j; ++m) {
                const auto &next = static_cast<DrawPathCommand *>(commands_[m].get())->data();
                merged.points.insert(merged.points.end(), next.points.begin(), next.points.end());
                if (first.hasCoverage()) {
                    merged.coverage.insert(merged.coverage.end(), next.coverage.begin(), next.coverage.end());
                }
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

bool Renderer::flushViaDeviceCommands()
{
    const int width = context_.getWidth();
    const int height = context_.getHeight();
    if (device_ == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    if (!mainTarget_ || mainTargetWidth_ != width || mainTargetHeight_ != height) {
        mainTarget_ = device_->createRenderTarget(width, height);
        mainTargetWidth_ = width;
        mainTargetHeight_ = height;
    }
    if (!mainTarget_) {
        return false;
    }

    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;

    const bool ok = device_->executeCommands(mainTarget_, commands_, request);
    if (ok) {
        stats_.drawCallCount += commands_.size();
    }
    return ok;
}
