// Renderer.cpp
#include "Renderer.h"

#include "command/DrawCommand.h"
#include "command/DrawPath.h"
#include "RenderDeviceFactory.h"
#include "IRenderTarget.h"
#include "SpriteBatch.h"

#include <cmath>
#include <iostream>
#include <iterator>
#include <glm/glm.hpp>

#include "core/LogInternal.h"

#include "render/GammaCorrect.h"
#include "render/PathMerge.h"

namespace {
constexpr float kMergeEpsilon = 0.001f;
constexpr std::size_t kMaxPathBatchVertices = 32768u;

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
        && (!data.hasRoundedCorners()
            || (nearlyEqual(data.u0, 0.0f)
                && nearlyEqual(data.v0, 0.0f)
                && nearlyEqual(data.u1, 1.0f)
                && nearlyEqual(data.v1, 1.0f)))
        && data.blendMode == blendMode;
}

bool isSpriteBatchCompatible(
    const DrawImageBatchData &data, const SharedImageResource &texture,
    DrawBlendMode blendMode)
{
    return data.imageResource == texture
        && data.imageResource
        && data.imageResource->isValid()
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

    // SpriteBatch owns GL programs and buffers. Release them while the caller's
    // graphics context is still current, before finalizing the render device.
    spriteBatch_.reset();

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

bool Renderer::supportsPresentation() const
{
    return device_ != nullptr && device_->supportsPresentation();
}

std::unique_ptr<ISwapchain> Renderer::createSwapchain(const NativeSurface &surface, const SwapchainConfig &config)
{
    return device_ == nullptr ? nullptr : device_->createSwapchain(surface, config);
}

bool Renderer::wrapBackendRenderTarget(const BackendRenderTarget &target)
{
    return device_ != nullptr && device_->wrapBackendRenderTarget(target);
}

std::uintptr_t Renderer::nativeHandle(int which) const
{
    return device_ == nullptr ? 0 : device_->nativeHandle(which);
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

SharedImageResource Renderer::renderQueuedCommandsToImageResource(
    size_t commandEnd, const OffscreenRenderRequest &request) const
{
    if (device_ == nullptr || commandEnd == 0 || commands_.empty()) {
        return {};
    }

    return commandEnd == commands_.size()
        ? renderCommandsToImageResource(commands_, request)
        : SharedImageResource();
}

SharedImageResource Renderer::filterImageResource(const SharedImageResource &source,
                                                  int width, int height,
                                                  const wsc::ImageFilter &filter,
                                                  FilterExecutionStats *executionStats) const
{
    if (executionStats != nullptr) {
        *executionStats = {};
    }
    if (device_ == nullptr) {
        return {};
    }
    FilterExecutionStats execution;
    SharedImageResource result =
        device_->filterImageResource(source, width, height, filter, &execution);
    if (executionStats != nullptr) {
        *executionStats = execution;
    }
    if (result && result->isValid()) {
        ++stats_.filterCount;
        stats_.filterPassCount += execution.passCount;
        stats_.downsampledFilterCount += execution.downsampled ? 1u : 0u;
        stats_.filterInputPixelCount +=
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        stats_.filterPixelPassCount += execution.pixelPassCount;
    }
    return result;
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
            WSC_LOG_ERROR("Renderer", "Device command execution failed; frame not rendered.");
        }
        return;
    }

    DrawPathProgram *pathProgram = DrawPathProgram::getInstance();
    pathProgram->beginFrame();

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
        if (commands_[i]->type() == Command::Type::ImageBatch) {
            const auto &first =
                static_cast<DrawImageBatchCommand *>(
                    commands_[i].get())->data();
            if (isSpriteBatchCompatible(
                    first, first.imageResource, first.blendMode)) {
                if (!spriteBatch_) {
                    spriteBatch_ = std::make_unique<SpriteBatch>();
                }
                spriteBatch_->clear();
                spriteBatch_->setTexture(first.imageResource);

                std::size_t j = i;
                while (j < commands_.size()
                       && commands_[j]->type()
                           == Command::Type::ImageBatch) {
                    const auto &batch =
                        static_cast<DrawImageBatchCommand *>(
                            commands_[j].get())->data();
                    if (!isSpriteBatchCompatible(
                            batch, first.imageResource,
                            first.blendMode)) {
                        break;
                    }
                    float tintColor[4] = {
                        batch.tintColor[0],
                        batch.tintColor[1],
                        batch.tintColor[2],
                        batch.tintColor[3] * batch.alpha
                    };
                    GammaCorrect::srgbToLinear4(tintColor);
                    for (const DrawImageBatchQuad &quad : batch.quads) {
                        spriteBatch_->add(
                            quad.x, quad.y, quad.width, quad.height,
                            quad.u0, quad.v0, quad.u1, quad.v1,
                            tintColor[0], tintColor[1], tintColor[2],
                            tintColor[3], batch.transform);
                    }
                    ++j;
                }

                if (!spriteBatch_->empty()) {
                    const std::size_t spriteCount =
                        spriteBatch_->spriteCount();
                    spriteBatch_->flush(context_, first.blendMode);
                    ++stats_.drawCallCount;
                    if (spriteCount > 1) {
                        ++stats_.mergedBatchCount;
                    }
                    i = j;
                    continue;
                }
            }
        }

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
                    if (!spriteBatch_) {
                        spriteBatch_ = std::make_unique<SpriteBatch>();
                    }
                    spriteBatch_->clear();
                    spriteBatch_->setTexture(first.imageResource);
                    for (std::size_t m = i; m < j; ++m) {
                        const auto &data = static_cast<DrawImageCommand *>(commands_[m].get())->data();
                        float tintColor[4] = {
                            data.tintColor[0],
                            data.tintColor[1],
                            data.tintColor[2],
                            data.tintColor[3] * data.alpha
                        };
                        GammaCorrect::srgbToLinear4(tintColor);
                        spriteBatch_->add(
                            data.x, data.y, data.width, data.height,
                            data.u0, data.v0, data.u1, data.v1,
                            tintColor[0], tintColor[1], tintColor[2],
                            tintColor[3], data.transform, data.roundedRadius);
                    }
                    spriteBatch_->flush(context_, first.blendMode);
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

        auto *pathCmd =
            static_cast<DrawPathCommand *>(commands_[i].get());
        const auto &first = pathCmd->data();

        std::size_t j = i + 1;
        std::size_t batchVertexCount = first.getPointCount();
        while (j < commands_.size()) {
            if (commands_[j]->type() != Command::Type::Path) {
                break;
            }

            auto *nextPathCmd = static_cast<DrawPathCommand *>(commands_[j].get());
            const auto &next = nextPathCmd->data();
            if (!wsc::render::canBatchPathData(first, next)) {
                break;
            }
            const std::size_t nextVertexCount = next.getPointCount();
            if (batchVertexCount + nextVertexCount
                    > kMaxPathBatchVertices) {
                break;
            }
            batchVertexCount += nextVertexCount;
            ++j;
        }

        if (j > i + 1) {
            std::size_t totalVertices = 0;
            bool needsVertexColors = first.hasVertexColors();
            bool needsCoverage = first.hasCoverage();
            bool flattenTransforms = false;
            for (std::size_t m = i; m < j; ++m) {
                const auto &next =
                    static_cast<DrawPathCommand *>(
                        commands_[m].get())->data();
                totalVertices += next.getPointCount();
                flattenTransforms =
                    flattenTransforms || next.transform != first.transform;
                needsVertexColors =
                    needsVertexColors || next.hasVertexColors();
                needsCoverage = needsCoverage || next.hasCoverage();
                for (int c = 0; c < 4; ++c) {
                    needsVertexColors =
                        needsVertexColors
                        || !nearlyEqual(first.color[c], next.color[c]);
                }
            }

            DrawPathData merged = first;
            merged.points.clear();
            merged.colors.clear();
            merged.coverage.clear();
            merged.vertexColorsLinear = needsVertexColors;
            if (flattenTransforms) {
                merged.transform = glm::mat4(1.0f);
            }
            merged.points.reserve(totalVertices * 2u);
            if (needsVertexColors) {
                merged.colors.reserve(totalVertices * 4u);
            }
            if (needsCoverage) {
                merged.coverage.reserve(totalVertices);
            }

            for (std::size_t m = i; m < j; ++m) {
                const auto &next =
                    static_cast<DrawPathCommand *>(
                        commands_[m].get())->data();
                const std::size_t vertexCount = next.getPointCount();
                if (flattenTransforms) {
                    for (std::size_t vertex = 0;
                         vertex < vertexCount; ++vertex) {
                        const glm::vec4 transformed =
                            next.transform
                            * glm::vec4(
                                next.points[vertex * 2u],
                                next.points[vertex * 2u + 1u],
                                0.0f, 1.0f);
                        merged.points.push_back(transformed.x);
                        merged.points.push_back(transformed.y);
                    }
                } else {
                    merged.points.insert(
                        merged.points.end(), next.points.begin(),
                        next.points.end());
                }
                if (needsVertexColors) {
                    if (next.hasVertexColors()) {
                        const std::size_t colorStart =
                            merged.colors.size();
                        merged.colors.insert(
                            merged.colors.end(), next.colors.begin(),
                            next.colors.end());
                        if (!next.vertexColorsLinear) {
                            for (std::size_t color = colorStart;
                                 color + 3u < merged.colors.size();
                                 color += 4u) {
                                GammaCorrect::srgbToLinear4(
                                    merged.colors.data() + color);
                            }
                        }
                    } else {
                        float linearColor[4] = {
                            next.color[0], next.color[1],
                            next.color[2], next.color[3]
                        };
                        GammaCorrect::srgbToLinear4(linearColor);
                        for (std::size_t vertex = 0;
                             vertex < vertexCount; ++vertex) {
                            merged.colors.insert(
                                merged.colors.end(),
                                std::begin(linearColor),
                                std::end(linearColor));
                        }
                    }
                }
                if (needsCoverage) {
                    if (next.hasCoverage()) {
                        merged.coverage.insert(
                            merged.coverage.end(),
                            next.coverage.begin(), next.coverage.end());
                    } else {
                        merged.coverage.insert(
                            merged.coverage.end(), vertexCount, 1.0f);
                    }
                }
            }

            DrawPathCommand mergedCmd(std::move(merged));
            mergedCmd.execute(context_);
            stats_.pathVertexCount += totalVertices;
            ++stats_.drawCallCount;
            ++stats_.mergedBatchCount;
            i = j;
        } else {
            stats_.pathVertexCount += first.getPointCount();
            executeCommand(commands_[i]);
            ++i;
        }
    }
    stats_.pathUploadCount += pathProgram->frameUploadCount();
    stats_.pathUploadBytes += pathProgram->frameUploadBytes();
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
        const std::size_t deviceDrawCalls =
            device_->lastExecutionDrawCallCount();
        stats_.drawCallCount +=
            deviceDrawCalls > 0 ? deviceDrawCalls : commands_.size();
        stats_.mergedBatchCount +=
            device_->lastExecutionMergedBatchCount();
    }
    return ok;
}
