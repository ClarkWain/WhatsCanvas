// Renderer.cpp
#include "Renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
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
constexpr std::size_t kMinIndependentPathReorderCount = 8u;
constexpr std::size_t kMaxIndependentPathReorderCount = 256u;

bool nearlyEqual(float lhs, float rhs)
{
    return std::abs(lhs - rhs) <= kMergeEpsilon;
}

std::uint64_t pathTopologyIdentity(const DrawPathData &data)
{
    if (!data.sharedGeometry) {
        return 0;
    }
    if (data.sharedGeometry->topologyFingerprint != 0) {
        return data.sharedGeometry->topologyFingerprint;
    }
    return static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(
            data.sharedGeometry.get()));
}

void reorderIndependentPathSegment(
    std::vector<std::unique_ptr<Command>> &commands,
    std::size_t begin, std::size_t end)
{
    if (end - begin < kMinIndependentPathReorderCount) {
        return;
    }
    const auto blendModeAt = [&](std::size_t index) {
        return static_cast<DrawPathCommand *>(
            commands[index].get())->data().blendMode;
    };
    const DrawBlendMode firstBlendMode = blendModeAt(begin);
    bool hasBlendTransition = false;
    for (std::size_t index = begin + 1u; index < end; ++index) {
        if (blendModeAt(index) != firstBlendMode) {
            hasBlendTransition = true;
            break;
        }
    }
    if (!hasBlendTransition) {
        return;
    }

    std::stable_sort(
        commands.begin() + static_cast<std::ptrdiff_t>(begin),
        commands.begin() + static_cast<std::ptrdiff_t>(end),
        [](const std::unique_ptr<Command> &lhs,
           const std::unique_ptr<Command> &rhs) {
            const DrawBlendMode lhsMode =
                static_cast<const DrawPathCommand *>(
                    lhs.get())->data().blendMode;
            const DrawBlendMode rhsMode =
                static_cast<const DrawPathCommand *>(
                    rhs.get())->data().blendMode;
            return static_cast<int>(lhsMode)
                < static_cast<int>(rhsMode);
        });
}

void reorderIndependentPathRuns(
    std::vector<std::unique_ptr<Command>> &commands)
{
    std::array<
        wsc::render::PathDeviceBounds,
        kMaxIndependentPathReorderCount> segmentBounds;
    std::size_t segmentSize = 0u;
    std::size_t segmentBegin = 0u;

    const auto finishSegment = [&]() {
        if (segmentSize > 0u) {
            reorderIndependentPathSegment(
                commands, segmentBegin,
                segmentBegin + segmentSize);
            segmentSize = 0u;
        }
    };

    for (std::size_t index = 0; index < commands.size(); ++index) {
        if (commands[index]->type() != Command::Type::Path) {
            finishSegment();
            continue;
        }

        const DrawPathData &data =
            static_cast<const DrawPathCommand *>(
                commands[index].get())->data();
        wsc::render::PathDeviceBounds bounds;
        if (!wsc::render::getReorderablePathBounds(data, bounds)) {
            finishSegment();
            continue;
        }

        bool overlaps = false;
        for (std::size_t candidate = 0u;
             candidate < segmentSize; ++candidate) {
            if (wsc::render::pathDeviceBoundsOverlap(
                    segmentBounds[candidate], bounds)) {
                overlaps = true;
                break;
            }
        }
        if (overlaps
            || segmentSize
                == kMaxIndependentPathReorderCount) {
            finishSegment();
        }
        if (segmentSize == 0u) {
            segmentBegin = index;
        }
        segmentBounds[segmentSize++] = bounds;
    }
    finishSegment();
}

void resetPathBatchState(
    const DrawPathData &source, DrawPathData &target)
{
    target.sharedGeometry.reset();
    target.points.clear();
    target.colors.clear();
    target.packedColors.clear();
    target.coverage.clear();
    target.indices.clear();
    target.drawIds.clear();
    target.drawParameters.clear();
    // packedCoverage and shortIndices may contain a reusable topology packet.
    // The caller clears them after deciding whether the cache still matches.

    target.width = source.width;
    std::copy_n(source.color, 4u, target.color);
    target.drawMode = source.drawMode;
    target.capStyle = source.capStyle;
    target.transform = source.transform;
    target.scissor = source.scissor;
    target.blendMode = source.blendMode;
    target.clipMask = source.clipMask;
    target.gradientType = source.gradientType;
    target.gradientTileMode = source.gradientTileMode;
    std::copy_n(source.gradientStart, 2u, target.gradientStart);
    std::copy_n(source.gradientEnd, 2u, target.gradientEnd);
    std::copy_n(source.radialCenter, 2u, target.radialCenter);
    target.radialRadius = source.radialRadius;
    target.gradientStopCount = source.gradientStopCount;
    target.gradientStops = source.gradientStops;
    target.vertexColorsLinear = source.vertexColorsLinear;
}

bool isSpriteBatchCompatible(
    const DrawImageData &data, DrawBlendMode blendMode)
{
    return data.imageResource
        && data.imageResource->isValid()
        && data.imageResource->nativeHandle().isValid()
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

void Renderer::abandonBackend()
{
    if (!backendInitialized_ || device_ == nullptr) {
        return;
    }

    // Mark device-created resources invalid first. Commands, targets and the
    // sprite batch can then drop their CPU owners without deleting names from
    // the context that has already disappeared.
    device_->abandonBackend();
    if (spriteBatch_) {
        spriteBatch_->abandonGLResources();
        spriteBatch_.reset();
    }
    mainTarget_.reset();
    mainTargetWidth_ = 0;
    mainTargetHeight_ = 0;
    commands_.clear();
    imageBatchAppendFloor_ = 0;
    pathBatchCaches_.clear();
    stats_.reset();
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

bool Renderer::tryAppendImageBatch(
    const DrawImageBatchData &batch)
{
    if (commands_.size() <= imageBatchAppendFloor_
        || batch.quads.empty()
        || batch.scissor.enabled || batch.clipMask.hasPaths()
        || batch.tintColor[0] != 1.0f
        || batch.tintColor[1] != 1.0f
        || batch.tintColor[2] != 1.0f
        || batch.tintColor[3] != 1.0f
        || batch.alpha != 1.0f
        || commands_.back()->type() != Command::Type::ImageBatch) {
        return false;
    }
    auto &previous = static_cast<DrawImageBatchCommand *>(
                         commands_.back().get())
                         ->data();
    if (previous.imageResource != batch.imageResource
        || previous.scissor.enabled
        || previous.clipMask.hasPaths()
        || previous.blendMode != batch.blendMode
        || previous.transform != batch.transform
        || previous.tintColor[0] != 1.0f
        || previous.tintColor[1] != 1.0f
        || previous.tintColor[2] != 1.0f
        || previous.tintColor[3] != 1.0f
        || previous.alpha != 1.0f) {
        return false;
    }
    previous.quads.insert(
        previous.quads.end(), batch.quads.begin(), batch.quads.end());
    return true;
}

size_t Renderer::commandCount() const
{
    // Canvas uses this value to mark layer command ranges. Prevent subsequent
    // batches from mutating a command that is now outside the new range.
    imageBatchAppendFloor_ = commands_.size();
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
    imageBatchAppendFloor_ = commands_.size();
    return taken;
}

void Renderer::appendCommands(std::vector<std::unique_ptr<Command>> &&commands)
{
    for (auto &command : commands) {
        commands_.push_back(std::move(command));
    }
    imageBatchAppendFloor_ = commands_.size();
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

SharedImageResource Renderer::createImageResourceAlpha8(
    int width, int height,
    const std::vector<unsigned char> &pixels) const
{
    return device_ == nullptr
        ? SharedImageResource()
        : device_->createImageResourceAlpha8(width, height, pixels);
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

bool Renderer::updateImageResourceAlpha8(
    const SharedImageResource &imageResource,
    int x, int y, int width, int height,
    const unsigned char *pixels) const
{
    return device_ != nullptr
        && device_->updateImageResourceAlpha8(
            imageResource, x, y, width, height, pixels);
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
        stats_.frameCompileCpuTimeNs +=
            device_->lastFrameCompileCpuTimeNs();
        stats_.compiledPacketCount +=
            device_->lastCompiledPacketCount();
        stats_.compiledVertexBytes +=
            device_->lastCompiledVertexBytes();
        stats_.compiledIndexBytes +=
            device_->lastCompiledIndexBytes();
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

void Renderer::recordGenericFilterPass(int width, int height) const
{
    ++stats_.filterCount;
    ++stats_.filterPassCount;
    const std::size_t pixels =
        static_cast<std::size_t>(std::max(width, 0))
        * static_cast<std::size_t>(std::max(height, 0));
    stats_.filterInputPixelCount += pixels;
    stats_.filterPixelPassCount += pixels;
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
    imageBatchAppendFloor_ = 0;
}

void Renderer::setGpuTimingEnabled(bool enabled)
{
    if (device_ != nullptr) {
        device_->setGpuFrameTimingEnabled(enabled);
    }
}

void Renderer::flush()
{
    const auto flushStart = std::chrono::steady_clock::now();
    const std::size_t drawCallsBeforeFlush =
        stats_.drawCallCount;
    const bool gpuTimerStarted =
        device_ != nullptr && device_->beginGpuFrameTiming();
    const auto finishTiming = [&]() {
        if (gpuTimerStarted) {
            device_->endGpuFrameTiming();
        }
        const auto flushEnd = std::chrono::steady_clock::now();
        stats_.flushCpuTimeNs += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                flushEnd - flushStart).count());
        std::uint64_t gpuTimeNs = 0;
        if (device_ != nullptr
            && device_->lastGpuFrameTimeNs(gpuTimeNs)) {
            stats_.gpuTimeNs = gpuTimeNs;
            stats_.gpuTimeAvailable = true;
        }
    };
    stats_.commandCount += commands_.size();

    // Devices such as Vulkan render the recorded command stream into a device
    // render target rather than executing each command against a GL context.
    // Never fall through to the GL execute path for these backends.
    if (device_ != nullptr && device_->usesDeviceCommandExecution()) {
        const auto deviceStart = std::chrono::steady_clock::now();
        if (!flushViaDeviceCommands()) {
            WSC_LOG_ERROR("Renderer", "Device command execution failed; frame not rendered.");
        }
        const auto deviceEnd = std::chrono::steady_clock::now();
        stats_.deviceExecutionCpuTimeNs += static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                deviceEnd - deviceStart).count());
        finishTiming();
        return;
    }

    DrawPathProgram *pathProgram = DrawPathProgram::getInstance();
    pathProgram->beginFrame();
    if (spriteBatch_) {
        spriteBatch_->beginFrame();
    }
    reorderIndependentPathRuns(commands_);
    std::size_t pathBatchCacheIndex = 0;

    auto executeCommand = [&](const std::unique_ptr<Command> &command) {
        pathProgram->endBatch();
        if (spriteBatch_) {
            spriteBatch_->endBatch();
        }
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
        if (commands_[i]->type() != Command::Type::Image
            && commands_[i]->type() != Command::Type::ImageBatch
            && spriteBatch_) {
            spriteBatch_->endBatch();
        }
        if (commands_[i]->type() != Command::Type::Path) {
            pathProgram->endBatch();
        }
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
                const bool compactGlyphBatch =
#if defined(WHATSCANVAS_OPENGL_ES)
                    false;
#else
                    first.imageResource->isAlphaOnly()
                    && first.transform[0][1] == 0.0f
                    && first.transform[1][0] == 0.0f;
#endif

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
                    if (compactGlyphBatch
                        && (batch.transform[0][1] != 0.0f
                            || batch.transform[1][0] != 0.0f)) {
                        break;
                    }
                    float tintColor[4] = {
                        batch.tintColor[0],
                        batch.tintColor[1],
                        batch.tintColor[2],
                        batch.tintColor[3] * batch.alpha
                    };
                    std::uint32_t cachedPackedTint = 0u;
                    float cachedTint[4] = {};
                    bool hasCachedTint = false;
                    for (const DrawImageBatchQuad &quad : batch.quads) {
                        if (!hasCachedTint
                            || cachedPackedTint
                                != quad.packedTint) {
                            cachedPackedTint = quad.packedTint;
                            cachedTint[0] = static_cast<float>(
                                quad.packedTint & 0xffu)
                                / 255.0f * tintColor[0];
                            cachedTint[1] = static_cast<float>(
                                (quad.packedTint >> 8u) & 0xffu)
                                / 255.0f * tintColor[1];
                            cachedTint[2] = static_cast<float>(
                                (quad.packedTint >> 16u) & 0xffu)
                                / 255.0f * tintColor[2];
                            cachedTint[3] = static_cast<float>(
                                (quad.packedTint >> 24u) & 0xffu)
                                / 255.0f * tintColor[3];
                            GammaCorrect::srgbToLinear4(cachedTint);
                            hasCachedTint = true;
                        }
                        if (compactGlyphBatch) {
                            spriteBatch_->addInstance(
                                quad.x, quad.y,
                                quad.width, quad.height,
                                quad.u0, quad.v0,
                                quad.u1, quad.v1,
                                cachedTint[0], cachedTint[1],
                                cachedTint[2], cachedTint[3],
                                batch.transform);
                        } else {
                            spriteBatch_->add(
                                quad.x, quad.y,
                                quad.width, quad.height,
                                quad.u0, quad.v0,
                                quad.u1, quad.v1,
                                cachedTint[0], cachedTint[1],
                                cachedTint[2], cachedTint[3],
                                batch.transform);
                        }
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
            if (isSpriteBatchCompatible(
                    first, first.blendMode)) {
                if (!spriteBatch_) {
                    spriteBatch_ =
                        std::make_unique<SpriteBatch>();
                }
                spriteBatch_->clear();

                std::size_t j = i;
                while (j < commands_.size()
                       && commands_[j]->type()
                           == Command::Type::Image) {
                    const auto &data =
                        static_cast<DrawImageCommand *>(
                            commands_[j].get())->data();
                    if (!isSpriteBatchCompatible(
                            data, first.blendMode)) {
                        break;
                    }
                    const int textureSlot =
                        spriteBatch_->addTexture(
                            data.imageResource);
                    if (textureSlot < 0) {
                        break;
                    }
                    float tintColor[4] = {
                        data.tintColor[0],
                        data.tintColor[1],
                        data.tintColor[2],
                        data.tintColor[3] * data.alpha
                    };
                    GammaCorrect::srgbToLinear4(tintColor);
                    spriteBatch_->add(
                        data.x, data.y,
                        data.width, data.height,
                        data.u0, data.v0,
                        data.u1, data.v1,
                        tintColor[0], tintColor[1],
                        tintColor[2], tintColor[3],
                        data.transform, data.roundedRadius,
                        textureSlot);
                    ++j;
                }

                if (j > i) {
                    spriteBatch_->flush(context_, first.blendMode);
                    ++stats_.drawCallCount;
                    if (j > i + 1) {
                        ++stats_.mergedBatchCount;
                    }
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
            bool canPackUniformColors =
                !first.hasVertexColors();
            bool needsCoverage = first.hasCoverage();
            bool needsIndices = first.hasIndices();
            bool flattenTransforms = false;
            bool immutableSharedTopology = true;
            bool canUseDrawParameters =
                (j - i) >= 64u
                && !first.hasVertexColors()
                && wsc::render::isAffine2DPathTransform(
                    first.transform);
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
                canPackUniformColors =
                    canPackUniformColors
                    && !next.hasVertexColors();
                needsCoverage = needsCoverage || next.hasCoverage();
                needsIndices = needsIndices || next.hasIndices();
                immutableSharedTopology =
                    immutableSharedTopology
                    && next.sharedGeometry != nullptr
                    && next.shortIndices.empty()
                    && next.packedCoverage.empty();
                canUseDrawParameters =
                    canUseDrawParameters
                    && !next.hasVertexColors()
                    && wsc::render::isAffine2DPathTransform(
                        next.transform);
                for (int c = 0; c < 4; ++c) {
                    needsVertexColors =
                        needsVertexColors
                        || !nearlyEqual(first.color[c], next.color[c]);
                }
            }

            if (pathBatchCacheIndex >= pathBatchCaches_.size()) {
                pathBatchCaches_.emplace_back();
            }
            PathBatchCache &batchCache =
                pathBatchCaches_[pathBatchCacheIndex++];
            DrawPathData &merged = batchCache.packet;
            resetPathBatchState(first, merged);
            bool useDrawParameters = false;
#if !defined(WHATSCANVAS_OPENGL_ES)
            useDrawParameters =
                canUseDrawParameters
                && (flattenTransforms || needsVertexColors);
#endif
            const bool usePackedColors =
                needsVertexColors && canPackUniformColors
                && !useDrawParameters;
            merged.vertexColorsLinear =
                needsVertexColors && !usePackedColors;
            const bool useShortIndices =
                needsIndices
                && totalVertices
                    <= static_cast<std::size_t>(
                        std::numeric_limits<std::uint16_t>::max())
                        + 1u;
            bool reuseSharedTopology =
                immutableSharedTopology
                && useShortIndices
                && batchCache.topology.size() == j - i
                && merged.shortIndices.size() == totalElements
                && (!needsCoverage
                    || merged.packedCoverage.size()
                        == totalVertices);
            if (reuseSharedTopology) {
                for (std::size_t m = i; m < j; ++m) {
                    const auto &next =
                        static_cast<DrawPathCommand *>(
                            commands_[m].get())->data();
                    if (batchCache.topology[m - i]
                            != pathTopologyIdentity(next)) {
                        reuseSharedTopology = false;
                        break;
                    }
                }
            }
            if (!reuseSharedTopology) {
                merged.shortIndices.clear();
                merged.packedCoverage.clear();
                if (immutableSharedTopology && useShortIndices) {
                    ++stats_.pathTopologyCacheMisses;
                }
            } else if (!needsCoverage) {
                merged.packedCoverage.clear();
                ++stats_.pathTopologyCacheHits;
            } else {
                ++stats_.pathTopologyCacheHits;
            }
            if (flattenTransforms || useDrawParameters) {
                merged.transform = glm::mat4(1.0f);
            }
            merged.points.reserve(totalVertices * 2u);
            if (useDrawParameters) {
                merged.drawIds.reserve(totalVertices);
                merged.drawParameters.reserve((j - i) * 10u);
            } else if (needsVertexColors) {
                if (usePackedColors) {
                    merged.packedColors.reserve(
                        totalVertices * 4u);
                } else {
                    merged.colors.reserve(totalVertices * 4u);
                }
            }
            if (needsCoverage) {
                if (!reuseSharedTopology) {
                    merged.packedCoverage.reserve(totalVertices);
                }
            }
            if (needsIndices) {
                if (useShortIndices) {
                    if (!reuseSharedTopology) {
                        merged.shortIndices.reserve(totalElements);
                    }
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
                if (useDrawParameters) {
                    std::copy(
                        nextPoints.begin(), nextPoints.end(),
                        merged.points.begin()
                            + static_cast<std::ptrdiff_t>(
                                pointStart));
                    const std::uint16_t drawId =
                        static_cast<std::uint16_t>(m - i);
                    merged.drawIds.insert(
                        merged.drawIds.end(),
                        vertexCount, drawId);
                    const glm::mat4 &transform = next.transform;
                    merged.drawParameters.insert(
                        merged.drawParameters.end(), {
                            transform[0][0], transform[1][0],
                            transform[3][0], transform[0][1],
                            transform[1][1], transform[3][1]
                        });
                    float shapeColor[4] = {
                        next.color[0], next.color[1],
                        next.color[2], next.color[3]
                    };
                    if (GammaCorrect::enabled()) {
                        GammaCorrect::srgbToLinear4(shapeColor);
                    }
                    merged.drawParameters.insert(
                        merged.drawParameters.end(),
                        std::begin(shapeColor),
                        std::end(shapeColor));
                } else if (flattenTransforms) {
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
                if (needsVertexColors && !useDrawParameters) {
                    if (usePackedColors) {
                        float linearColor[4] = {
                            next.color[0], next.color[1],
                            next.color[2], next.color[3]
                        };
                        if (GammaCorrect::enabled()) {
                            GammaCorrect::srgbToLinear4(
                                linearColor);
                        }
                        std::uint8_t packedColor[4] = {};
                        for (std::size_t channel = 0;
                             channel < 4u; ++channel) {
                            packedColor[channel] =
                                static_cast<std::uint8_t>(
                                    std::clamp(
                                        std::lround(
                                            linearColor[channel]
                                            * 255.0f),
                                        0l, 255l));
                        }
                        const std::size_t colorStart =
                            merged.packedColors.size();
                        merged.packedColors.resize(
                            colorStart + vertexCount * 4u);
                        for (std::size_t vertex = 0;
                             vertex < vertexCount; ++vertex) {
                            std::copy_n(
                                packedColor, 4u,
                                merged.packedColors.begin()
                                    + static_cast<std::ptrdiff_t>(
                                        colorStart
                                        + vertex * 4u));
                        }
                    } else if (next.hasFloatVertexColors()) {
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
                    } else if (next.hasPackedVertexColors()) {
                        const std::size_t colorStart =
                            merged.colors.size();
                        merged.colors.resize(
                            colorStart + vertexCount * 4u);
                        for (std::size_t color = 0;
                             color < next.packedColors.size();
                             ++color) {
                            merged.colors[colorStart + color] =
                                static_cast<float>(
                                    next.packedColors[color])
                                / 255.0f;
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
                if (needsCoverage && !reuseSharedTopology) {
                    if (next.hasPackedCoverage()) {
                        const std::vector<std::uint8_t> &packed =
                            next.packedCoverageData();
                        merged.packedCoverage.insert(
                            merged.packedCoverage.end(),
                            packed.begin(), packed.end());
                    } else if (next.hasFloatCoverage()) {
                        const std::size_t coverageStart =
                            merged.packedCoverage.size();
                        merged.packedCoverage.resize(
                            coverageStart + vertexCount);
                        for (std::size_t vertex = 0;
                             vertex < vertexCount; ++vertex) {
                            merged.packedCoverage[
                                coverageStart + vertex] =
                                static_cast<std::uint8_t>(
                                    std::clamp(
                                        std::lround(
                                            nextCoverage[vertex]
                                            * 255.0f),
                                        0l, 255l));
                        }
                    } else {
                        merged.packedCoverage.insert(
                            merged.packedCoverage.end(),
                            vertexCount, 255u);
                    }
                }
                if (needsIndices && !reuseSharedTopology) {
                    const std::size_t incomingIndexCount =
                        next.hasIndices()
                            ? next.getElementCount()
                            : vertexCount;
                    if (useShortIndices) {
                        const std::size_t indexStart =
                            merged.shortIndices.size();
                        merged.shortIndices.resize(
                            indexStart + incomingIndexCount);
                        auto output =
                            merged.shortIndices.begin()
                            + static_cast<std::ptrdiff_t>(
                                indexStart);
                        if (next.hasShortIndices()) {
                            std::transform(
                                next.shortIndices.begin(),
                                next.shortIndices.end(), output,
                                [baseVertex](std::uint16_t index) {
                                    return static_cast<std::uint16_t>(
                                        baseVertex + index);
                                });
                        } else if (next.hasLongIndices()) {
                            const std::vector<std::uint32_t> &indices =
                                next.indexData();
                            std::transform(
                                indices.begin(), indices.end(), output,
                                [baseVertex](std::uint32_t index) {
                                    return static_cast<std::uint16_t>(
                                        baseVertex + index);
                                });
                        } else {
                            for (std::size_t index = 0;
                                 index < incomingIndexCount; ++index) {
                                output[
                                    static_cast<std::ptrdiff_t>(index)] =
                                    static_cast<std::uint16_t>(
                                        baseVertex + index);
                            }
                        }
                    } else {
                        const std::size_t indexStart =
                            merged.indices.size();
                        merged.indices.resize(
                            indexStart + incomingIndexCount);
                        auto output =
                            merged.indices.begin()
                            + static_cast<std::ptrdiff_t>(
                                indexStart);
                        if (next.hasShortIndices()) {
                            std::transform(
                                next.shortIndices.begin(),
                                next.shortIndices.end(), output,
                                [baseVertex](std::uint16_t index) {
                                    return baseVertex + index;
                                });
                        } else if (next.hasLongIndices()) {
                            const std::vector<std::uint32_t> &indices =
                                next.indexData();
                            std::transform(
                                indices.begin(), indices.end(), output,
                                [baseVertex](std::uint32_t index) {
                                    return baseVertex + index;
                                });
                        } else {
                            for (std::size_t index = 0;
                                 index < incomingIndexCount; ++index) {
                                output[
                                    static_cast<std::ptrdiff_t>(index)] =
                                    baseVertex
                                    + static_cast<std::uint32_t>(index);
                            }
                        }
                    }
                }
            }

            if (immutableSharedTopology
                && useShortIndices
                && !reuseSharedTopology) {
                batchCache.topology.clear();
                batchCache.topology.reserve(j - i);
                for (std::size_t m = i; m < j; ++m) {
                    const auto &next =
                        static_cast<DrawPathCommand *>(
                            commands_[m].get())->data();
                    batchCache.topology.push_back(
                        pathTopologyIdentity(next));
                }
            } else if (!immutableSharedTopology
                       || !useShortIndices) {
                batchCache.topology.clear();
            }

            context_.applyClipState(
                merged.scissor, merged.clipMask);
            context_.applyBlendMode(merged.blendMode);
            pathProgram->beginBatch();
            pathProgram->draw(context_, merged);
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
            context_.applyClipState(
                first.scissor, first.clipMask);
            context_.applyBlendMode(first.blendMode);
            pathProgram->beginBatch();
            pathProgram->draw(context_, first);
            ++stats_.drawCallCount;
            ++i;
        }
    }
    pathProgram->endBatch();
    if (spriteBatch_) {
        spriteBatch_->endBatch();
    }
    stats_.pathUploadCount += pathProgram->frameUploadCount();
    stats_.pathUploadBytes += pathProgram->frameUploadBytes();
    stats_.pathIndexBytes += pathProgram->frameIndexBytes();
    stats_.compiledPacketCount +=
        stats_.drawCallCount - drawCallsBeforeFlush;
    stats_.compiledVertexBytes += pathProgram->frameUploadBytes()
        - std::min(pathProgram->frameUploadBytes(),
                   pathProgram->frameIndexBytes());
    stats_.compiledIndexBytes += pathProgram->frameIndexBytes();
    finishTiming();
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
        stats_.compiledPacketCount +=
            device_->lastCompiledPacketCount() > 0
                ? device_->lastCompiledPacketCount()
                : deviceDrawCalls;
        stats_.compiledVertexBytes +=
            device_->lastCompiledVertexBytes();
        stats_.compiledIndexBytes +=
            device_->lastCompiledIndexBytes();
        stats_.frameCompileCpuTimeNs +=
            device_->lastFrameCompileCpuTimeNs();
    }
    return ok;
}
