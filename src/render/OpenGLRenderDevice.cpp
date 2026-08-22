#include "render/OpenGLRenderDevice.h"

#include <algorithm>
#include <cstring>

#include <glad/glad.h>

#include "command/DrawCommand.h"
#include "command/DrawImage.h"
#include "command/DrawLines.h"
#include "command/DrawPath.h"
#include "command/DrawPoints.h"
#include "command/DrawText.h"
#include "opengl/GlobalIndexBuffers.h"
#include "opengl/GaussianBlurProgram.h"
#include "opengl/GLProgram.h"
#include "opengl/GLTextureUtils.h"
#include "opengl/PixelFormatCaps.h"
#include "opengl/ClipCoverageProgram.h"
#include "opengl/DrawClipFillProgram.h"
#include "render/IRenderer.h"
#include "render/IRenderTarget.h"
#include "render/RenderContext.h"
#include "render/GammaCorrect.h"
#include "render/FrameCompiler.h"
#include "render/RenderTargetPool.h"
#include "render/SpriteBatch.h"
#include "render/GLPresent.h"

struct OpenGLContextState
{
    bool abandoned = false;
};

namespace {

int g_renderDeviceBackendRefCount = 0;
std::size_t g_activeImageTextureResourceCount = 0;
std::size_t g_activeRenderTargetResourceCount = 0;

class OpenGLImageResource final : public ImageResource
{
public:
    explicit OpenGLImageResource(ImageResourceHandle handle, bool ownsHandle,
                                  ImageOrigin origin, ImageAlphaType alphaType,
                                  bool alphaOnly,
                                  std::shared_ptr<OpenGLContextState> contextState)
        : handle_(handle),
          ownsHandle_(ownsHandle),
          origin_(origin),
          alphaType_(alphaType),
          alphaOnly_(alphaOnly),
          contextState_(std::move(contextState))
    {
        if (handle_.isValid()) {
            ++g_activeImageTextureResourceCount;
        }
    }

    ~OpenGLImageResource() override
    {
        if (handle_.isValid() && g_activeImageTextureResourceCount > 0) {
            --g_activeImageTextureResourceCount;
        }
        if (ownsHandle_ && handle_.isValid()
            && contextState_ && !contextState_->abandoned) {
            wsc::opengl::destroyTexture(handle_);
        }
    }

    bool isValid() const override
    {
        return handle_.isValid() && contextState_ && !contextState_->abandoned;
    }

    ImageOrigin origin() const override { return origin_; }
    void setOrigin(ImageOrigin origin) { origin_ = origin; }
    ImageAlphaType alphaType() const override { return alphaType_; }
    void setAlphaType(ImageAlphaType alphaType) { alphaType_ = alphaType; }
    bool isAlphaOnly() const override { return alphaOnly_; }
    ImageResourceHandle nativeHandle() const override { return handle_; }

    void bind(const RenderContext &context) const override
    {
        context.bindImageHandle(handle_);
    }

    bool updateRGBA(int x, int y, int width, int height, const unsigned char *pixels,
                    bool regenerateMipmaps) override
    {
        return wsc::opengl::updateTextureRGBA(handle_, x, y, width, height, pixels, regenerateMipmaps);
    }

    bool updateAlpha8(
        int x, int y, int width, int height,
        const unsigned char *pixels)
    {
        return alphaOnly_
            && wsc::opengl::updateTextureAlpha8(
                handle_, x, y, width, height, pixels);
    }

    GLuint texture() const { return static_cast<GLuint>(handle_.value); }

private:
    ImageResourceHandle handle_;
    bool ownsHandle_ = true;
    ImageOrigin origin_ = ImageOrigin::TopLeft;
    ImageAlphaType alphaType_ = ImageAlphaType::Straight;
    bool alphaOnly_ = false;
    std::shared_ptr<OpenGLContextState> contextState_;
};

class OpenGLClipMaskResource final : public ClipMaskResource
{
public:
    explicit OpenGLClipMaskResource(const ClipMaskPath &maskPath)
        : points_(maskPath.points),
          coverage_(maskPath.coverage),
          transform_(maskPath.transform)
    {
    }

    bool isValid() const override
    {
        return !points_.empty();
    }

    void apply(const RenderContext &context, const ScissorState &scissor, std::size_t clipIndex) const override
    {
        (void)scissor;
        (void)clipIndex;
        if (!isValid()) {
            return;
        }
        // Rasterise this clip's anti-aliased coverage into the temp layer that
        // the coverage program bound before calling us.
        wsc::opengl::ClipCoverageProgram::getInstance()->drawCoverage(
            points_, coverage_, transform_, context.getWidth(), context.getHeight());
    }

private:
    std::vector<float> points_;
    std::vector<float> coverage_;
    glm::mat4 transform_ = glm::mat4(1.0f);
};

class OpenGLRenderTarget final : public IRenderTarget
{
public:
    OpenGLRenderTarget(int width, int height, SharedImageResource imageResource,
                       GLuint framebuffer, GLuint stencilRenderbuffer,
                       std::shared_ptr<OpenGLContextState> contextState)
        : width_(width),
          height_(height),
          imageResource_(std::move(imageResource)),
          framebuffer_(framebuffer),
          stencilRenderbuffer_(stencilRenderbuffer),
          contextState_(std::move(contextState))
    {
        if (framebuffer_ != 0) {
            ++g_activeRenderTargetResourceCount;
        }
    }

    ~OpenGLRenderTarget() override
    {
        if (framebuffer_ != 0 && g_activeRenderTargetResourceCount > 0) {
            --g_activeRenderTargetResourceCount;
        }
        if (stencilRenderbuffer_ != 0 && contextState_
            && !contextState_->abandoned) {
            glDeleteRenderbuffers(1, &stencilRenderbuffer_);
        }
        if (framebuffer_ != 0 && contextState_
            && !contextState_->abandoned) {
            glDeleteFramebuffers(1, &framebuffer_);
        }
    }

    bool isValid() const override
    {
        return contextState_ && !contextState_->abandoned
            && width_ > 0 && height_ > 0 && framebuffer_ != 0 && stencilRenderbuffer_ != 0
            && imageResource_ && imageResource_->isValid();
    }

    int width() const override
    {
        return width_;
    }

    int height() const override
    {
        return height_;
    }

    bool begin(const OffscreenRenderRequest &request) override
    {
        if (!isValid()) {
            return false;
        }

        // A pooled target may previously have been returned as a filtered
        // straight-alpha image. Offscreen command replay always produces the
        // backend's canonical bottom-left, premultiplied render-target image.
        if (auto *image = dynamic_cast<OpenGLImageResource *>(imageResource_.get())) {
            image->setOrigin(ImageOrigin::BottomLeft);
            image->setAlphaType(ImageAlphaType::Premultiplied);
        }

        // Lazy mode: only store the request, don't bind FBO yet.
        request_ = request;
        begun_ = true;
        activated_ = false;
        return true;
    }

    GLuint framebuffer() const { return framebuffer_; }

    void activate() override
    {
        if (activated_ || !begun_) {
            return;
        }

        // Save previous GL state.
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer_);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture_);
        glGetIntegerv(GL_VIEWPORT, previousViewport_);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor_);

        // Bind FBO and set viewport.
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glViewport(request_.viewportX, request_.viewportY,
                   request_.canvasWidth, request_.canvasHeight);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

        activated_ = true;
    }

    bool isActivated() const override
    {
        return activated_;
    }

    void end() override
    {
        if (!begun_) {
            return;
        }

        if (activated_) {
            // Restore previous GL state only if we actually bound the FBO.
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer_));
            glViewport(previousViewport_[0], previousViewport_[1],
                       previousViewport_[2], previousViewport_[3]);
            glClearColor(previousClearColor_[0], previousClearColor_[1],
                         previousClearColor_[2], previousClearColor_[3]);
            glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(previousTexture_));
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_STENCIL_TEST);
        }

        begun_ = false;
        activated_ = false;
    }

    SharedImageResource getImageResource() const override
    {
        return imageResource_;
    }

private:
    int width_ = 0;
    int height_ = 0;
    SharedImageResource imageResource_;
    GLuint framebuffer_ = 0;
    GLuint stencilRenderbuffer_ = 0;
    std::shared_ptr<OpenGLContextState> contextState_;

    // Lazy activation state.
    bool begun_ = false;
    bool activated_ = false;
    OffscreenRenderRequest request_;

    // Saved GL state (populated on activate).
    GLint previousFramebuffer_ = 0;
    GLint previousTexture_ = 0;
    GLint previousViewport_[4] = {0, 0, 0, 0};
    GLfloat previousClearColor_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

SharedImageResource createSharedOpenGLImageResource(
    const std::shared_ptr<OpenGLContextState> &contextState,
    ImageResourceHandle handle, bool ownsHandle = true,
    ImageOrigin origin = ImageOrigin::TopLeft,
    ImageAlphaType alphaType = ImageAlphaType::Straight,
    bool alphaOnly = false)
{
    if (!handle.isValid()) {
        return {};
    }

    return std::make_shared<OpenGLImageResource>(
        handle, ownsHandle, origin, alphaType, alphaOnly, contextState);
}

void initializeSharedRenderBackend()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#if !defined(WHATSCANVAS_OPENGL_ES)
    if (GammaCorrect::enabled()) {
        glEnable(GL_FRAMEBUFFER_SRGB);
    } else {
        glDisable(GL_FRAMEBUFFER_SRGB);
    }
#endif
    PixelFormatCaps::initialize();
    GlobalIndexBuffers::initialize();

    // Draw programs create their context-bound resources on first use. This
    // avoids compiling legacy or uncommon shader paths during startup while
    // preserving the shared singleton lifetime and context-loss teardown.
}

void finalizeSharedRenderBackend()
{
    // These programs are initialized lazily, but still own context-bound GL
    // objects (FBOs, textures, VAOs and VBOs).  They must be released together
    // with the eagerly initialized programs before an EGL context is replaced.
    // Otherwise a recreated Android surface can reuse stale object names from
    // the previous context and render into incomplete framebuffers.
    wsc::opengl::GaussianBlurProgram::getInstance()->release();
    wsc::opengl::ClipCoverageProgram::getInstance()->release();
    wsc::opengl::DrawClipFillProgram::getInstance()->release();

    DrawPointsProgram::getInstance()->release();
    DrawLinesProgram::getInstance()->release();
    DrawPathProgram::getInstance()->release();
    DrawImageProgram::getInstance()->release();
    DrawTextProgram::getInstance()->release();
    GlobalIndexBuffers::finalize();
    PixelFormatCaps::reset();
}

void abandonSharedRenderBackend()
{
    wsc::opengl::GaussianBlurProgram::getInstance()->release(true);
    wsc::opengl::ClipCoverageProgram::getInstance()->release(true);
    wsc::opengl::DrawClipFillProgram::getInstance()->release(true);

    DrawPointsProgram::getInstance()->release(true);
    DrawLinesProgram::getInstance()->release(true);
    DrawPathProgram::getInstance()->release(true);
    DrawImageProgram::getInstance()->release(true);
    DrawTextProgram::getInstance()->release(true);
    GlobalIndexBuffers::abandon();
    PixelFormatCaps::reset();
}

float fromNdcX(float x, int width)
{
    return (x + 1.0f) * 0.5f * static_cast<float>(width);
}

float fromNdcY(float y, int height)
{
    return (y + 1.0f) * 0.5f * static_cast<float>(height);
}

DrawBlendMode blendModeFromDrawListIndex(int mode)
{
    switch (mode) {
    case 1:
        return DrawBlendMode::Src;
    case 2:
        return DrawBlendMode::Add;
    case 3:
        return DrawBlendMode::Multiply;
    case 4:
        return DrawBlendMode::Screen;
    case 5:
        return DrawBlendMode::Dst;
    case 6:
        return DrawBlendMode::Clear;
    case 7:
        return DrawBlendMode::SrcIn;
    case 8:
        return DrawBlendMode::DstIn;
    case 9:
        return DrawBlendMode::SrcOut;
    case 10:
        return DrawBlendMode::DstOut;
    case 11:
        return DrawBlendMode::SrcAtop;
    case 12:
        return DrawBlendMode::DstAtop;
    case 13:
        return DrawBlendMode::Xor;
    default:
        return DrawBlendMode::SrcOver;
    }
}

} // namespace

OpenGLRenderDevice::OpenGLRenderDevice() = default;

OpenGLRenderDevice::~OpenGLRenderDevice()
{
    finalizeBackend();
}

void OpenGLRenderDevice::initializeBackend()
{
    if (backendInitialized_) {
        return;
    }

    contextState_ = std::make_shared<OpenGLContextState>();
    if (g_renderDeviceBackendRefCount == 0) {
        initializeSharedRenderBackend();
    }

    ++g_renderDeviceBackendRefCount;
    backendInitialized_ = true;
#if !defined(WHATSCANVAS_OPENGL_ES)
    if (glGenQueries != nullptr && glBeginQuery != nullptr
        && glGetQueryObjectiv != nullptr
        && glGetQueryObjectui64v != nullptr) {
        glGenQueries(3, gpuTimerQueries_);
    }
#endif
}

void OpenGLRenderDevice::abandonBackend()
{
    if (!backendInitialized_) {
        return;
    }

    if (contextState_) {
        contextState_->abandoned = true;
    }
    if (renderTargetPool_) {
        renderTargetPool_->clear();
    }
    if (offscreenSpriteBatch_) {
        offscreenSpriteBatch_->abandonGLResources();
        offscreenSpriteBatch_.reset();
    }

    std::fill(std::begin(gpuTimerQueries_), std::end(gpuTimerQueries_), 0u);
    std::fill(std::begin(gpuTimerPending_), std::end(gpuTimerPending_), false);
    std::fill(std::begin(gpuTimerSequences_), std::end(gpuTimerSequences_), 0u);
    activeGpuTimerQuery_ = -1;
    nextGpuTimerQuery_ = 0;
    nextGpuTimerSequence_ = 1;
    lastGpuTimeNs_ = 0;
    lastGpuTimeSequence_ = 0;
    lastGpuTimeAvailable_ = false;

    if (g_renderDeviceBackendRefCount > 0) {
        --g_renderDeviceBackendRefCount;
        if (g_renderDeviceBackendRefCount == 0) {
            abandonSharedRenderBackend();
        }
    }
    hasWrappedFramebuffer_ = false;
    wrappedFramebuffer_ = 0;
    backendInitialized_ = false;
}

void OpenGLRenderDevice::finalizeBackend()
{
    if (!backendInitialized_) {
        return;
    }

    if (renderTargetPool_) {
        renderTargetPool_->clear();
    }
    offscreenSpriteBatch_.reset();

#if !defined(WHATSCANVAS_OPENGL_ES)
    if (gpuTimerQueries_[0] != 0u && glDeleteQueries != nullptr) {
        glDeleteQueries(3, gpuTimerQueries_);
    }
#endif
    std::fill(std::begin(gpuTimerQueries_), std::end(gpuTimerQueries_), 0u);
    std::fill(std::begin(gpuTimerPending_), std::end(gpuTimerPending_), false);
    std::fill(std::begin(gpuTimerSequences_), std::end(gpuTimerSequences_), 0u);
    activeGpuTimerQuery_ = -1;
    nextGpuTimerQuery_ = 0;
    nextGpuTimerSequence_ = 1;
    lastGpuTimeNs_ = 0;
    lastGpuTimeSequence_ = 0;
    lastGpuTimeAvailable_ = false;

    if (g_renderDeviceBackendRefCount > 0) {
        --g_renderDeviceBackendRefCount;
        if (g_renderDeviceBackendRefCount == 0) {
            finalizeSharedRenderBackend();
        }
    }

    if (contextState_) {
        // Any externally retained Image now belongs to a context that is no
        // longer usable. Its destructor must not delete the old object name
        // after a replacement context becomes current.
        contextState_->abandoned = true;
    }
    backendInitialized_ = false;
}

bool OpenGLRenderDevice::beginGpuFrameTiming()
{
#if !defined(WHATSCANVAS_OPENGL_ES)
    if (!gpuTimingEnabled_ || !backendInitialized_
        || gpuTimerQueries_[0] == 0u
        || activeGpuTimerQuery_ >= 0) {
        return false;
    }

    std::uint64_t newestTimeNs = lastGpuTimeNs_;
    std::uint64_t newestSequence = lastGpuTimeSequence_;
    for (int index = 0; index < 3; ++index) {
        if (!gpuTimerPending_[index]) {
            continue;
        }
        GLint available = GL_FALSE;
        glGetQueryObjectiv(
            gpuTimerQueries_[index], GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == GL_TRUE) {
            GLuint64 elapsed = 0;
            glGetQueryObjectui64v(
                gpuTimerQueries_[index], GL_QUERY_RESULT, &elapsed);
            if (gpuTimerSequences_[index] > newestSequence) {
                newestTimeNs = static_cast<std::uint64_t>(elapsed);
                newestSequence = gpuTimerSequences_[index];
            }
            gpuTimerPending_[index] = false;
        }
    }
    if (newestSequence > lastGpuTimeSequence_) {
        lastGpuTimeNs_ = newestTimeNs;
        lastGpuTimeSequence_ = newestSequence;
        lastGpuTimeAvailable_ = true;
    }

    for (int offset = 0; offset < 3; ++offset) {
        const int index = (nextGpuTimerQuery_ + offset) % 3;
        if (gpuTimerPending_[index]) {
            continue;
        }
        glBeginQuery(GL_TIME_ELAPSED, gpuTimerQueries_[index]);
        gpuTimerSequences_[index] = nextGpuTimerSequence_++;
        activeGpuTimerQuery_ = index;
        nextGpuTimerQuery_ = (index + 1) % 3;
        return true;
    }
#endif
    return false;
}

void OpenGLRenderDevice::endGpuFrameTiming()
{
#if !defined(WHATSCANVAS_OPENGL_ES)
    if (activeGpuTimerQuery_ >= 0) {
        glEndQuery(GL_TIME_ELAPSED);
        gpuTimerPending_[activeGpuTimerQuery_] = true;
        activeGpuTimerQuery_ = -1;
    }
#endif
}

bool OpenGLRenderDevice::lastGpuFrameTimeNs(
    std::uint64_t &nanoseconds) const
{
    nanoseconds = lastGpuTimeAvailable_ ? lastGpuTimeNs_ : 0;
    return lastGpuTimeAvailable_;
}

bool OpenGLRenderDevice::readPixelsRGBA(int width, int height, std::vector<unsigned char> &pixels) const
{
    if (width <= 0 || height <= 0) {
        pixels.clear();
        return false;
    }

    const size_t rowSize = static_cast<size_t>(width) * 4;
    const size_t bufferSize = rowSize * static_cast<size_t>(height);
    std::vector<unsigned char> bottomUp(bufferSize);
    pixels.resize(bufferSize);

    GLint previousReadFramebuffer = 0;
    if (hasWrappedFramebuffer_) {
        glGetIntegerv(
            GL_READ_FRAMEBUFFER_BINDING,
            &previousReadFramebuffer);
        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            static_cast<GLuint>(wrappedFramebuffer_));
        if (wrappedFramebuffer_ != 0) {
            glReadBuffer(GL_COLOR_ATTACHMENT0);
        }
    }

    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

    if (hasWrappedFramebuffer_) {
        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            static_cast<GLuint>(previousReadFramebuffer));
    }

    for (int y = 0; y < height; ++y) {
        const size_t srcOffset = static_cast<size_t>(height - 1 - y) * rowSize;
        const size_t dstOffset = static_cast<size_t>(y) * rowSize;
        std::copy(bottomUp.begin() + srcOffset, bottomUp.begin() + srcOffset + rowSize,
                  pixels.begin() + dstOffset);
    }

    return true;
}

std::unique_ptr<IRenderTarget> OpenGLRenderDevice::createRenderTarget(int width, int height) const
{
    if (width <= 0 || height <= 0) {
        return {};
    }

    GLuint framebuffer = 0;
    GLuint stencilRenderbuffer = 0;
    ImageResourceHandle texture;
    if (!wsc::opengl::createRenderTargetTexture(width, height, framebuffer, stencilRenderbuffer, texture)) {
        if (stencilRenderbuffer != 0) {
            glDeleteRenderbuffers(1, &stencilRenderbuffer);
        }
        if (framebuffer != 0) {
            glDeleteFramebuffers(1, &framebuffer);
        }
        return {};
    }

    return std::make_unique<OpenGLRenderTarget>(
        width, height,
        createSharedOpenGLImageResource(
            contextState_, texture, true, ImageOrigin::BottomLeft,
            ImageAlphaType::Premultiplied),
        framebuffer, stencilRenderbuffer, contextState_);
}

SharedClipMaskResource OpenGLRenderDevice::createClipMaskResource(const ClipMaskPath &maskPath) const
{
    if (maskPath.points.empty()) {
        return {};
    }

    return std::make_shared<OpenGLClipMaskResource>(maskPath);
}

SharedImageResource OpenGLRenderDevice::createImageResourceRGBA(int width, int height,
                                                                const std::vector<unsigned char> &pixels) const
{
    return createSharedOpenGLImageResource(
        contextState_, wsc::opengl::createTextureRGBA(width, height, pixels));
}

SharedImageResource OpenGLRenderDevice::createImageResourceAlpha8(
    int width, int height,
    const std::vector<unsigned char> &pixels) const
{
    return createSharedOpenGLImageResource(
        contextState_, wsc::opengl::createTextureAlpha8(width, height, pixels),
        true, ImageOrigin::TopLeft, ImageAlphaType::Straight, true);
}

SharedImageResource OpenGLRenderDevice::createImageResourceFromImageData(int width, int height, int channels,
                                                                         const unsigned char *pixels,
                                                                         bool generateMipmaps) const
{
    return createSharedOpenGLImageResource(
        contextState_, wsc::opengl::createTextureFromImageData(
            width, height, channels, pixels, generateMipmaps));
}

bool OpenGLRenderDevice::updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y,
                                                 int width, int height, const unsigned char *pixels,
                                                 bool regenerateMipmaps) const
{
    return imageResource && imageResource->isValid()
        && imageResource->updateRGBA(x, y, width, height, pixels, regenerateMipmaps);
}

bool OpenGLRenderDevice::updateImageResourceAlpha8(
    const SharedImageResource &imageResource,
    int x, int y, int width, int height,
    const unsigned char *pixels) const
{
    auto *texture =
        dynamic_cast<OpenGLImageResource *>(imageResource.get());
    return texture != nullptr && texture->isValid()
        && texture->updateAlpha8(
            x, y, width, height, pixels);
}

SharedImageResource OpenGLRenderDevice::wrapExternalImageResource(ImageResourceHandle handle) const
{
    return createSharedOpenGLImageResource(contextState_, handle, false);
}

RenderResourceStats OpenGLRenderDevice::resourceStats() const
{
    RenderResourceStats stats;
    stats.imageTextureCount = g_activeImageTextureResourceCount;
    stats.renderTargetCount = g_activeRenderTargetResourceCount;
    const GLProgramCompilationStats shaderStats =
        GLProgram::compilationStats();
    stats.shaderProgramLinkCount = shaderStats.programLinkCount;
    stats.shaderStageCompileCount = shaderStats.shaderCompileCount;
    stats.shaderCompileCpuTimeNs = shaderStats.shaderCompileCpuTimeNs;
    stats.shaderLinkCpuTimeNs = shaderStats.programLinkCpuTimeNs;
    if (renderTargetPool_) {
        stats.pooledRenderTargetCount = renderTargetPool_->pooledCount();
        stats.pooledRenderTargetBytes = renderTargetPool_->pooledBytes();
        stats.renderTargetPoolReuseCount = renderTargetPool_->reuseCount();
        stats.renderTargetPoolAllocationCount = renderTargetPool_->allocationCount();
        stats.renderTargetPoolEvictionCount = renderTargetPool_->evictionCount();
    }
    return stats;
}

bool OpenGLRenderDevice::executeDrawList(const wsc::DrawList &drawList,
                                         int canvasWidth, int canvasHeight,
                                         int targetHeight) const
{
    if (canvasWidth <= 0 || canvasHeight <= 0 || targetHeight <= 0) {
        return false;
    }

    RenderContext context;
    context.setSize(canvasWidth, canvasHeight);
    if (!offscreenSpriteBatch_) {
        offscreenSpriteBatch_ = std::make_unique<SpriteBatch>();
    }
    SpriteBatch &spriteBatch = *offscreenSpriteBatch_;
    spriteBatch.beginFrame();
    DrawPathProgram *pathProgram = DrawPathProgram::getInstance();
    bool pathBatchActive = false;
    const auto endPathBatch = [&]() {
        if (pathBatchActive) {
            pathProgram->endBatch();
            pathBatchActive = false;
        }
    };
    const auto fail = [&]() {
        endPathBatch();
        spriteBatch.endBatch();
        return false;
    };

    for (const wsc::DrawPrimitive &prim : drawList) {
        ScissorState scissor;
        if (prim.scissorEnabled) {
            scissor.enabled = true;
            scissor.x = prim.scissorX;
            // DrawPrimitive scissors are already resolved into the cropped
            // target by CommandDrawListEncoder. Convert from its top-left
            // convention to GL bottom-left exactly once.
            scissor.y = targetHeight
                - (prim.scissorY + prim.scissorHeight);
            scissor.width = prim.scissorWidth;
            scissor.height = prim.scissorHeight;
        }
        const DrawBlendMode blendMode = blendModeFromDrawListIndex(prim.blendMode);
        context.applyScissorState(scissor);
        context.applyBlendMode(blendMode);

        if (prim.kind == wsc::DrawPrimitiveKind::SolidTriangles || prim.kind == wsc::DrawPrimitiveKind::GradientFill) {
            const std::size_t vertexCount = prim.positions.size() / 2u;
            const std::size_t elementCount =
                prim.indices.empty() ? vertexCount : prim.indices.size();
            if ((prim.positions.size() % 2u) != 0u || vertexCount < 3
                || elementCount < 3 || (elementCount % 3u) != 0u) {
                return fail();
            }
            for (const std::uint32_t index : prim.indices) {
                if (index >= vertexCount) {
                    return fail();
                }
            }
            DrawPathData data;
            data.points.reserve(prim.positions.size());
            for (std::size_t i = 0; i < vertexCount; ++i) {
                data.points.push_back(fromNdcX(
                    prim.positions[i * 2u + 0u], canvasWidth));
                data.points.push_back(fromNdcY(
                    prim.positions[i * 2u + 1u], canvasHeight));
            }
            data.indices = prim.indices;
            data.colors = prim.colors;
            data.coverage = prim.coverage;
            data.color[0] = prim.color[0];
            data.color[1] = prim.color[1];
            data.color[2] = prim.color[2];
            data.color[3] = prim.color[3];
            data.drawMode = PathDrawMode::Fill;
            data.capStyle = PathCapStyle::Round;
            data.scissor = scissor;
            data.blendMode = blendMode;
            if (prim.kind == wsc::DrawPrimitiveKind::GradientFill) {
                data.gradientType = static_cast<DrawGradientType>(prim.gradientType);
                data.gradientTileMode = static_cast<DrawGradientTileMode>(prim.gradientTileMode);
                data.gradientStart[0] = prim.linearStart[0];
                data.gradientStart[1] = prim.linearStart[1];
                data.gradientEnd[0] = prim.linearEnd[0];
                data.gradientEnd[1] = prim.linearEnd[1];
                data.radialCenter[0] = prim.radialCenter[0];
                data.radialCenter[1] = prim.radialCenter[1];
                data.radialRadius = prim.radialRadius;
                data.gradientStopCount = prim.gradientStopCount;
                DrawPathGradientStops &stops =
                    data.writableGradientStops();
                std::memcpy(
                    stops.positions, prim.gradientStopPositions,
                    sizeof(stops.positions));
                std::memcpy(
                    stops.colors, prim.gradientStopColors,
                    sizeof(stops.colors));
            }
            if (!pathBatchActive) {
                pathProgram->beginBatch();
                pathBatchActive = true;
            }
            pathProgram->draw(context, data);
        } else if (prim.kind == wsc::DrawPrimitiveKind::TexturedQuad) {
            endPathBatch();
            if (!prim.texture || prim.positions.size() < 12u
                || prim.positions.size() % 12u != 0u
                || prim.uvs.size() != prim.positions.size()) {
                return fail();
            }
            const std::size_t quadCount = prim.positions.size() / 12u;
            if (quadCount > 1u || !prim.packedTints.empty()) {
                spriteBatch.clear();
                spriteBatch.setTexture(prim.texture);
                for (std::size_t quad = 0; quad < quadCount; ++quad) {
                    const std::size_t base = quad * 12u;
                    const float x = fromNdcX(
                        prim.positions[base], canvasWidth);
                    const float y = fromNdcY(
                        prim.positions[base + 1u], canvasHeight);
                    const float right = fromNdcX(
                        prim.positions[base + 4u], canvasWidth);
                    const float bottom = fromNdcY(
                        prim.positions[base + 5u], canvasHeight);
                    float tint[4] = {
                        prim.tint[0], prim.tint[1], prim.tint[2],
                        prim.tint[3] * prim.layerAlpha,
                    };
                    if (!prim.packedTints.empty()) {
                        const std::uint32_t packed =
                            prim.packedTints[quad * 6u];
                        tint[0] *= static_cast<float>(packed & 0xffu)
                            / 255.0f;
                        tint[1] *= static_cast<float>(
                            (packed >> 8u) & 0xffu) / 255.0f;
                        tint[2] *= static_cast<float>(
                            (packed >> 16u) & 0xffu) / 255.0f;
                        tint[3] *= static_cast<float>(
                            (packed >> 24u) & 0xffu) / 255.0f;
                    }
                    GammaCorrect::srgbToLinear4(tint);
                    spriteBatch.add(
                        x, y, right - x, bottom - y,
                        prim.uvs[base], prim.uvs[base + 1u],
                        prim.uvs[base + 4u], prim.uvs[base + 5u],
                        tint[0], tint[1], tint[2], tint[3]);
                }
                spriteBatch.flush(context, blendMode);
                spriteBatch.endBatch();
                continue;
            }
            DrawImageData data;
            data.imageResource = prim.texture;
            data.x = fromNdcX(prim.positions[0], canvasWidth);
            data.y = fromNdcY(prim.positions[1], canvasHeight);
            const float x1 = fromNdcX(prim.positions[4], canvasWidth);
            const float y1 = fromNdcY(prim.positions[5], canvasHeight);
            data.width = x1 - data.x;
            data.height = y1 - data.y;
            data.u0 = prim.uvs[0];
            data.v0 = prim.uvs[1];
            data.u1 = prim.uvs[4];
            data.v1 = prim.uvs[5];
            data.alpha = prim.layerAlpha;
            data.roundedRadius = prim.roundedRadius;
            data.tintColor[0] = prim.tint[0];
            data.tintColor[1] = prim.tint[1];
            data.tintColor[2] = prim.tint[2];
            data.tintColor[3] = prim.tint[3];
            data.hasColorMatrix = prim.hasColorMatrix;
            if (prim.hasColorMatrix) {
                std::memcpy(data.colorMatrix, prim.colorMatrix, sizeof(data.colorMatrix));
                std::memcpy(data.colorMatrixOffset, prim.colorMatrixOffset, sizeof(data.colorMatrixOffset));
            }
            data.sampling = static_cast<DrawImageSampling>(prim.sampling);
            data.tileMode = static_cast<DrawImageTileMode>(prim.tileMode);
            data.scissor = scissor;
            data.blendMode = blendMode;
            if (prim.gradientType != 0 && prim.gradientStopCount > 0) {
                data.gradientType = static_cast<DrawGradientType>(prim.gradientType);
                data.gradientTileMode = static_cast<DrawGradientTileMode>(prim.gradientTileMode);
                data.gradientStart[0] = prim.linearStart[0];
                data.gradientStart[1] = prim.linearStart[1];
                data.gradientEnd[0] = prim.linearEnd[0];
                data.gradientEnd[1] = prim.linearEnd[1];
                data.radialCenter[0] = prim.radialCenter[0];
                data.radialCenter[1] = prim.radialCenter[1];
                data.radialRadius = prim.radialRadius;
                data.gradientStopCount = prim.gradientStopCount;
                std::memcpy(data.gradientStopPositions, prim.gradientStopPositions, sizeof(data.gradientStopPositions));
                std::memcpy(data.gradientStopColors, prim.gradientStopColors, sizeof(data.gradientStopColors));
            }
            DrawImageProgram::getInstance()->draw(context, data);
        } else if (prim.kind == wsc::DrawPrimitiveKind::ClipFill) {
            endPathBatch();
            if (!prim.texture || !prim.texture->isValid()) {
                return fail();
            }
            wsc::opengl::DrawClipFillData data;
            data.positions = &prim.positions;
            data.uvs = &prim.uvs;
            if (!prim.colors.empty()) {
                data.perVertexColors = &prim.colors;
            }
            data.color[0] = prim.color[0];
            data.color[1] = prim.color[1];
            data.color[2] = prim.color[2];
            data.color[3] = prim.color[3];
            data.mask = prim.texture;
            auto *clipProgram = wsc::opengl::DrawClipFillProgram::getInstance();
            clipProgram->initialize();
            clipProgram->draw(context, data);
        } else {
            return fail();
        }
    }

    endPathBatch();
    return true;
}

bool OpenGLRenderDevice::supportsPresentation() const
{
    return wsc::gl::glPresentSupported();
}

std::unique_ptr<ISwapchain> OpenGLRenderDevice::createSwapchain(const NativeSurface &surface,
                                                               const SwapchainConfig &config)
{
    return wsc::gl::makeGLSwapchain(surface, config);
}

bool OpenGLRenderDevice::wrapBackendRenderTarget(const BackendRenderTarget &target)
{
    if (target.kind == BackendRenderTarget::Kind::None) {
        hasWrappedFramebuffer_ = false;
        wrappedFramebuffer_ = 0;
        return true;
    }
    if (target.kind != BackendRenderTarget::Kind::OpenGLFramebuffer) {
        return false;
    }
    // Direct subsequent drawing into the host's framebuffer. WhatsCanvas draws
    // into the currently-bound FBO; offscreen passes (saveLayer/blur/clip) save
    // and restore GL_FRAMEBUFFER_BINDING, so this binding is honored throughout.
    hasWrappedFramebuffer_ = true;
    wrappedFramebuffer_ = target.glFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(target.glFramebuffer));
    if (target.width > 0 && target.height > 0) {
        glViewport(0, 0, target.width, target.height);
    }
    return true;
}

SharedImageResource OpenGLRenderDevice::renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                                      const OffscreenRenderRequest &request) const
{
    lastCompiledPacketCount_ = 0;
    lastCompiledVertexBytes_ = 0;
    lastCompiledIndexBytes_ = 0;
    lastFrameCompileCpuTimeNs_ = 0;
    if (commands.empty() || request.canvasWidth <= 0 || request.canvasHeight <= 0 ||
        request.targetWidth <= 0 || request.targetHeight <= 0) {
        return {};
    }

    if (!renderTargetPool_) {
        renderTargetPool_ = std::make_unique<RenderTargetPool>(this);
    }

    std::unique_ptr<IRenderTarget> renderTarget = renderTargetPool_->acquire(request.targetWidth, request.targetHeight);
    if (!renderTarget || !renderTarget->begin(request)) {
        return {};
    }

    renderTarget->activate();
    bool rendered = false;
    {
        CommandDrawListEncodeRequest compileRequest;
        compileRequest.canvasWidth = request.canvasWidth;
        compileRequest.canvasHeight = request.canvasHeight;
        compileRequest.targetHeight = request.targetHeight;
        compileRequest.scissorOffsetX = request.scissorOffsetX;
        compileRequest.scissorOffsetY = request.scissorOffsetY;
        CompiledFrame frame;
        FrameCompiler compiler;
        if (compiler.compile(commands, compileRequest, frame)) {
            bool portable = true;
            for (const wsc::DrawPrimitive &packet : frame.packets) {
                if (packet.kind == wsc::DrawPrimitiveKind::GaussianShadow
                    || packet.kind == wsc::DrawPrimitiveKind::ClipFill
                    || packet.clipTexture
                    || (packet.kind == wsc::DrawPrimitiveKind::TexturedQuad
                        && (!packet.texturedInstances.empty()
                            || packet.gradientType != 0
                            || (packet.positions.size() != 12u
                                && (packet.hasColorMatrix
                                    || packet.sampling != 0
                                    || packet.tileMode != 0))))) {
                    portable = false;
                    break;
                }
                if (packet.kind == wsc::DrawPrimitiveKind::TexturedQuad) {
                    if (packet.positions.empty()
                        || packet.positions.size() % 12u != 0u
                        || packet.uvs.size() != packet.positions.size()
                        || (!packet.packedTints.empty()
                            && packet.packedTints.size()
                                != packet.positions.size() / 2u)) {
                        portable = false;
                        break;
                    }
                    const auto near = [](float left, float right) {
                        return std::fabs(left - right) <= 1.0e-5f;
                    };
                    const std::vector<float> &p = packet.positions;
                    for (std::size_t base = 0; base < p.size();
                         base += 12u) {
                        const bool axisAligned =
                            near(p[base + 1u], p[base + 3u])
                            && near(p[base + 1u], p[base + 7u])
                            && near(p[base + 2u], p[base + 4u])
                            && near(p[base + 4u], p[base + 8u])
                            && near(p[base + 5u], p[base + 9u])
                            && near(p[base + 5u], p[base + 11u])
                            && near(p[base + 6u], p[base + 10u])
                            && near(p[base], p[base + 6u]);
                        if (!axisAligned) {
                            portable = false;
                            break;
                        }
                        if (!packet.packedTints.empty()) {
                            const std::uint32_t tint =
                                packet.packedTints[base / 2u];
                            for (std::size_t vertex = 1; vertex < 6u;
                                 ++vertex) {
                                if (packet.packedTints[base / 2u + vertex]
                                    != tint) {
                                    portable = false;
                                    break;
                                }
                            }
                        }
                        if (!portable) {
                            break;
                        }
                    }
                }
                if (!portable) {
                    break;
                }
            }
            if (portable) {
                rendered = executeDrawList(
                    frame.packets, request.canvasWidth,
                    request.canvasHeight, request.targetHeight);
                if (rendered) {
                    lastCompiledPacketCount_ =
                        frame.stats.packetCount;
                    lastCompiledVertexBytes_ =
                        frame.stats.vertexBytes;
                    lastCompiledIndexBytes_ =
                        frame.stats.indexBytes;
                    lastFrameCompileCpuTimeNs_ =
                        frame.stats.cpuTimeNs;
                }
            }
        }
    }
    if (!rendered) {
        // The direct path remains the correctness fallback for complex clip
        // masks and packet kinds not covered by portable replay.
        RenderContext context;
        context.setSize(request.canvasWidth, request.canvasHeight);
        context.setScissorOffset(
            request.scissorOffsetX, request.scissorOffsetY);
        for (const std::unique_ptr<Command> &command : commands) {
            if (command) {
                command->execute(context);
            }
        }
    }
    renderTarget->end();
    SharedImageResource imageResource = renderTarget->getImageResource();
    renderTargetPool_->release(std::move(renderTarget));
    renderTargetPool_->expire();
    return imageResource;
}

SharedImageResource OpenGLRenderDevice::filterImageResource(const SharedImageResource &source,
                                                            int width, int height,
                                                            const wsc::ImageFilter &filter,
                                                            FilterExecutionStats *executionStats) const
{
    if (executionStats != nullptr) {
        *executionStats = {};
    }
    const auto *input = dynamic_cast<const OpenGLImageResource *>(source.get());
    if (input == nullptr || !input->isValid() || width <= 0 || height <= 0
        || !filter.isValid()) {
        return {};
    }
    const bool innerShadow =
        filter.type() == wsc::ImageFilter::Type::InnerShadow;
    if (!innerShadow && filter.type() != wsc::ImageFilter::Type::Blur) {
        return {};
    }

    const wsc::render::GaussianBlurDownsample downsample =
        wsc::render::chooseGaussianBlurDownsampleFactors(
        width, height, filter.radiusX(), filter.radiusY());
    const int blurWidth = (width + downsample.x - 1) / downsample.x;
    const int blurHeight = (height + downsample.y - 1) / downsample.y;
    if (!renderTargetPool_) {
        renderTargetPool_ = std::make_unique<RenderTargetPool>(this);
    }
    std::unique_ptr<IRenderTarget> targetA =
        renderTargetPool_->acquire(blurWidth, blurHeight);
    std::unique_ptr<IRenderTarget> targetB =
        renderTargetPool_->acquire(blurWidth, blurHeight);
    auto *glTargetA = dynamic_cast<OpenGLRenderTarget *>(targetA.get());
    auto *glTargetB = dynamic_cast<OpenGLRenderTarget *>(targetB.get());
    if (glTargetA == nullptr || glTargetB == nullptr
        || !glTargetA->isValid() || !glTargetB->isValid()) {
        return {};
    }
    const auto *imageA = dynamic_cast<const OpenGLImageResource *>(glTargetA->getImageResource().get());
    const auto *imageB = dynamic_cast<const OpenGLImageResource *>(glTargetB->getImageResource().get());
    if (imageA == nullptr || !imageA->isValid()
        || imageB == nullptr || !imageB->isValid()) {
        return {};
    }
    std::unique_ptr<IRenderTarget> restoreTarget;
    OpenGLRenderTarget *glRestoreTarget = nullptr;
    if (downsample.active() || innerShadow) {
        restoreTarget = renderTargetPool_->acquire(width, height);
        glRestoreTarget = dynamic_cast<OpenGLRenderTarget *>(restoreTarget.get());
        if (glRestoreTarget == nullptr || !glRestoreTarget->isValid()) {
            return {};
        }
    }

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {};
    GLint previousProgram = 0;
    GLint previousVertexArray = 0;
    GLint previousActiveTexture = 0;
    GLint previousTexture0 = 0;
    GLint previousTexture1 = 0;
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture1);
    glActiveTexture(GL_TEXTURE0);

    auto *blur = wsc::opengl::GaussianBlurProgram::getInstance();
    blur->initialize();
    const auto kernelX = wsc::render::computeGaussianKernel(
        filter.radiusX() / static_cast<float>(downsample.x));
    const auto kernelY = wsc::render::computeGaussianKernel(
        filter.radiusY() / static_cast<float>(downsample.y));
    const bool decal = innerShadow
        || filter.tileMode() == wsc::ImageFilter::TileMode::Decal;
    const bool sourcePremultiplied =
        source->alphaType() == ImageAlphaType::Premultiplied;
    blur->blurImagePass(input->texture(), glTargetA->framebuffer(), blurWidth, blurHeight,
                        glm::vec2(1.0f / static_cast<float>(blurWidth), 0.0f),
                        kernelX, decal, 1.0f, 1.0f, 1.0f, 0.0f,
                        sourcePremultiplied, false,
                        downsample.active() && !sourcePremultiplied);
    blur->blurImagePass(imageA->texture(), glTargetB->framebuffer(), blurWidth, blurHeight,
                        glm::vec2(0.0f, 1.0f / static_cast<float>(blurHeight)),
                        kernelY, decal,
                        !innerShadow && !downsample.active() ? filter.saturation() : 1.0f,
                        !innerShadow && !downsample.active() ? filter.brightness() : 1.0f,
                        !innerShadow && !downsample.active() ? filter.contrast() : 1.0f,
                        !innerShadow && !downsample.active() ? filter.grain() : 0.0f,
                        true, !innerShadow && !downsample.active());
    if (innerShadow) {
        const float textureOffsetY =
            source->origin() == ImageOrigin::BottomLeft
            ? -filter.offsetY() : filter.offsetY();
        const wsc::Color shadow = filter.shadowColor();
        blur->innerShadowPass(
            imageB->texture(), input->texture(),
            glRestoreTarget->framebuffer(), width, height,
            glm::vec2(filter.offsetX() / static_cast<float>(width),
                      textureOffsetY / static_cast<float>(height)),
            glm::vec4(shadow.r(), shadow.g(), shadow.b(), shadow.a()),
            sourcePremultiplied);
    } else if (downsample.active()) {
        const auto passThrough = wsc::render::computeGaussianKernel(0.0f);
        blur->blurImagePass(imageB->texture(), glRestoreTarget->framebuffer(), width, height,
                            glm::vec2(0.0f), passThrough, decal,
                            filter.saturation(), filter.brightness(), filter.contrast(),
                            filter.grain(), true, true);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glUseProgram(static_cast<GLuint>(previousProgram));
    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture0));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture1));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    if (blendEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (scissorEnabled) {
        glEnable(GL_SCISSOR_TEST);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

    if (executionStats != nullptr) {
        executionStats->passCount =
            (downsample.active() || innerShadow) ? 3u : 2u;
        executionStats->pixelPassCount =
            static_cast<std::size_t>(blurWidth) * static_cast<std::size_t>(blurHeight) * 2u
            + ((downsample.active() || innerShadow)
                ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
                : 0u);
        executionStats->downsampled = downsample.active();
    }
    SharedImageResource result = (downsample.active() || innerShadow)
        ? glRestoreTarget->getImageResource()
        : glTargetB->getImageResource();
    if (auto *output = dynamic_cast<OpenGLImageResource *>(result.get())) {
        output->setOrigin(source->origin());
        output->setAlphaType(ImageAlphaType::Straight);
    }
    renderTargetPool_->release(std::move(targetA));
    renderTargetPool_->release(std::move(targetB));
    renderTargetPool_->release(std::move(restoreTarget));
    renderTargetPool_->expire();
    return result;
}
