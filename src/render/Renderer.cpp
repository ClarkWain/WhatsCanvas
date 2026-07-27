// Renderer.cpp
#include "Renderer.h"

#include <algorithm>
#include <cstdint>
#include <limits>

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
// Indexed AA keeps dense frames compact enough to submit in one stream packet.
// 65K vertices still bounds temporary merge storage while avoiding an
// otherwise artificial batch break in the 1080p geometry stress scene.
constexpr std::size_t kMaxPathBatchVertices = 65536u;

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
            std::size_t totalElements = 0;
            bool needsVertexColors = first.hasVertexColors();
            bool needsCoverage = first.hasCoverage();
            bool needsIndices = first.hasIndices();
            bool flattenTransforms = false;
            for (std::size_t m = i; m < j; ++m) {
                const auto &next =
                    static_cast<DrawPathCommand *>(
                        commands_[m].get())->data();
                totalVertices += next.getPointCount();
                totalElements += next.getElementCount();
                flattenTransforms =
                    flattenTransforms || next.transform != first.transform;
                needsVertexColors =
                    needsVertexColors || next.hasVertexColors();
                needsCoverage = needsCoverage || next.hasCoverage();
                needsIndices = needsIndices || next.hasIndices();
                for (int c = 0; c < 4; ++c) {
                    needsVertexColors =
                        needsVertexColors
                        || !nearlyEqual(first.color[c], next.color[c]);
                }
            }

            DrawPathData merged = first;
            merged.sharedGeometry.reset();
            merged.points.clear();
            merged.colors.clear();
            merged.coverage.clear();
            merged.indices.clear();
            merged.shortIndices.clear();
            merged.vertexColorsLinear = needsVertexColors;
            const bool useShortIndices =
                needsIndices
                && totalVertices
                    <= static_cast<std::size_t>(
                        std::numeric_limits<std::uint16_t>::max())
                        + 1u;
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
            if (needsIndices) {
                if (useShortIndices) {
                    merged.shortIndices.reserve(totalElements);
                } else {
                    merged.indices.reserve(totalElements);
                }
            }

            for (std::size_t m = i; m < j; ++m) {
                const auto &next =
                    static_cast<DrawPathCommand *>(
                        commands_[m].get())->data();
                const std::vector<float> &nextPoints =
                    next.pointData();
                const std::vector<float> &nextCoverage =
                    next.coverageData();
                const std::size_t vertexCount = next.getPointCount();
                const std::uint32_t baseVertex =
                    static_cast<std::uint32_t>(
                        merged.points.size() / 2u);
                const std::size_t pointStart =
                    merged.points.size();
                merged.points.resize(
                    pointStart + vertexCount * 2u);
                if (flattenTransforms) {
                    const glm::mat4 &transform = next.transform;
                    for (std::size_t vertex = 0;
                         vertex < vertexCount; ++vertex) {
                        const float x =
                            nextPoints[vertex * 2u];
                        const float y =
                            nextPoints[vertex * 2u + 1u];
                        merged.points[
                            pointStart + vertex * 2u] =
                            transform[0][0] * x
                            + transform[1][0] * y
                            + transform[3][0];
                        merged.points[
                            pointStart + vertex * 2u + 1u] =
                            transform[0][1] * x
                            + transform[1][1] * y
                            + transform[3][1];
                    }
                } else {
                    std::copy(
                        nextPoints.begin(), nextPoints.end(),
                        merged.points.begin()
                            + static_cast<std::ptrdiff_t>(
                                pointStart));
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
                        const std::size_t colorStart =
                            merged.colors.size();
                        merged.colors.resize(
                            colorStart + vertexCount * 4u);
                        for (std::size_t vertex = 0;
                             vertex < vertexCount; ++vertex) {
                            std::copy_n(
                                linearColor, 4u,
                                merged.colors.begin()
                                    + static_cast<std::ptrdiff_t>(
                                        colorStart
                                        + vertex * 4u));
                        }
                    }
                }
                if (needsCoverage) {
                    if (next.hasCoverage()) {
                        merged.coverage.insert(
                            merged.coverage.end(),
                            nextCoverage.begin(), nextCoverage.end());
                    } else {
                        merged.coverage.insert(
                            merged.coverage.end(), vertexCount, 1.0f);
                    }
                }
                if (needsIndices) {
                    const std::size_t incomingIndexCount =
                        next.hasIndices()
                            ? next.getElementCount()
                            : vertexCount;
                    if (useShortIndices) {
                        const std::size_t indexStart =
                            merged.shortIndices.size();
                        merged.shortIndices.resize(
                            indexStart + incomingIndexCount);
                        for (std::size_t index = 0;
                             index < incomingIndexCount; ++index) {
                            const std::uint32_t sourceIndex =
                                next.hasIndices()
                                    ? next.getIndex(index)
                                    : static_cast<std::uint32_t>(
                                        index);
                            merged.shortIndices[
                                indexStart + index] =
                                static_cast<std::uint16_t>(
                                    baseVertex + sourceIndex);
                        }
                    } else {
                        const std::size_t indexStart =
                            merged.indices.size();
                        merged.indices.resize(
                            indexStart + incomingIndexCount);
                        for (std::size_t index = 0;
                             index < incomingIndexCount; ++index) {
                            const std::uint32_t sourceIndex =
                                next.hasIndices()
                                    ? next.getIndex(index)
                                    : static_cast<std::uint32_t>(
                                        index);
                            merged.indices[indexStart + index] =
                                baseVertex + sourceIndex;
                        }
                    }
                }
            }

            DrawPathCommand mergedCmd(std::move(merged));
            mergedCmd.execute(context_);
            stats_.pathVertexCount += totalVertices;
            if (needsIndices) {
                stats_.pathIndexCount += totalElements;
            }
            ++stats_.drawCallCount;
            ++stats_.mergedBatchCount;
            i = j;
        } else {
            stats_.pathVertexCount += first.getPointCount();
            stats_.pathIndexCount += first.hasIndices()
                ? first.getElementCount() : 0u;
            executeCommand(commands_[i]);
            ++i;
        }
    }
    stats_.pathUploadCount += pathProgram->frameUploadCount();
    stats_.pathUploadBytes += pathProgram->frameUploadBytes();
    stats_.pathIndexBytes += pathProgram->frameIndexBytes();
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
