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
#include "opengl/GLTextureUtils.h"
#include "opengl/PixelFormatCaps.h"
#include "opengl/ClipCoverageProgram.h"
#include "opengl/DrawClipFillProgram.h"
#include "render/IRenderer.h"
#include "render/IRenderTarget.h"
#include "render/RenderContext.h"
#include "render/GammaCorrect.h"
#include "render/RenderTargetPool.h"
#include "render/GLPresent.h"

namespace {

int g_renderDeviceBackendRefCount = 0;
std::size_t g_activeImageTextureResourceCount = 0;
std::size_t g_activeRenderTargetResourceCount = 0;

class OpenGLImageResource final : public ImageResource
{
public:
    explicit OpenGLImageResource(ImageResourceHandle handle, bool ownsHandle)
        : handle_(handle),
          ownsHandle_(ownsHandle)
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
        if (ownsHandle_ && handle_.isValid()) {
            wsc::opengl::destroyTexture(handle_);
        }
    }

    bool isValid() const override
    {
        return handle_.isValid();
    }

    void bind(const RenderContext &context) const override
    {
        context.bindImageHandle(handle_);
    }

    bool updateRGBA(int x, int y, int width, int height, const unsigned char *pixels,
                    bool regenerateMipmaps) override
    {
        return wsc::opengl::updateTextureRGBA(handle_, x, y, width, height, pixels, regenerateMipmaps);
    }

    GLuint texture() const { return static_cast<GLuint>(handle_.value); }

private:
    ImageResourceHandle handle_;
    bool ownsHandle_ = true;
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
                       GLuint framebuffer, GLuint stencilRenderbuffer)
        : width_(width),
          height_(height),
          imageResource_(std::move(imageResource)),
          framebuffer_(framebuffer),
          stencilRenderbuffer_(stencilRenderbuffer)
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
        if (stencilRenderbuffer_ != 0) {
            glDeleteRenderbuffers(1, &stencilRenderbuffer_);
        }
        if (framebuffer_ != 0) {
            glDeleteFramebuffers(1, &framebuffer_);
        }
    }

    bool isValid() const override
    {
        return width_ > 0 && height_ > 0 && framebuffer_ != 0 && stencilRenderbuffer_ != 0
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

SharedImageResource createSharedOpenGLImageResource(ImageResourceHandle handle, bool ownsHandle = true)
{
    if (!handle.isValid()) {
        return {};
    }

    return std::make_shared<OpenGLImageResource>(handle, ownsHandle);
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

    DrawPointsProgram::getInstance()->initialize();
    DrawLinesProgram::getInstance()->initialize();
    DrawPathProgram::getInstance()->initialize();
    DrawImageProgram::getInstance()->initialize();
    DrawTextProgram::getInstance()->initialize();
}

void finalizeSharedRenderBackend()
{
    DrawPointsProgram::getInstance()->release();
    DrawLinesProgram::getInstance()->release();
    DrawPathProgram::getInstance()->release();
    DrawImageProgram::getInstance()->release();
    DrawTextProgram::getInstance()->release();
    GlobalIndexBuffers::finalize();
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

OpenGLRenderDevice::~OpenGLRenderDevice()
{
    finalizeBackend();
}

void OpenGLRenderDevice::initializeBackend()
{
    if (backendInitialized_) {
        return;
    }

    if (g_renderDeviceBackendRefCount == 0) {
        initializeSharedRenderBackend();
    }

    ++g_renderDeviceBackendRefCount;
    backendInitialized_ = true;
}

void OpenGLRenderDevice::finalizeBackend()
{
    if (!backendInitialized_) {
        return;
    }

    if (renderTargetPool_) {
        renderTargetPool_->clear();
    }

    if (g_renderDeviceBackendRefCount > 0) {
        --g_renderDeviceBackendRefCount;
        if (g_renderDeviceBackendRefCount == 0) {
            finalizeSharedRenderBackend();
        }
    }

    backendInitialized_ = false;
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

    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

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

    return std::make_unique<OpenGLRenderTarget>(width, height, createSharedOpenGLImageResource(texture),
                                                framebuffer, stencilRenderbuffer);
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
    return createSharedOpenGLImageResource(wsc::opengl::createTextureRGBA(width, height, pixels));
}

SharedImageResource OpenGLRenderDevice::createImageResourceFromImageData(int width, int height, int channels,
                                                                         const unsigned char *pixels,
                                                                         bool generateMipmaps) const
{
    return createSharedOpenGLImageResource(
        wsc::opengl::createTextureFromImageData(width, height, channels, pixels, generateMipmaps));
}

bool OpenGLRenderDevice::updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y,
                                                 int width, int height, const unsigned char *pixels,
                                                 bool regenerateMipmaps) const
{
    return imageResource && imageResource->isValid()
        && imageResource->updateRGBA(x, y, width, height, pixels, regenerateMipmaps);
}

SharedImageResource OpenGLRenderDevice::wrapExternalImageResource(ImageResourceHandle handle) const
{
    return createSharedOpenGLImageResource(handle, false);
}

RenderResourceStats OpenGLRenderDevice::resourceStats() const
{
    RenderResourceStats stats;
    stats.imageTextureCount = g_activeImageTextureResourceCount;
    stats.renderTargetCount = g_activeRenderTargetResourceCount;
    return stats;
}

bool OpenGLRenderDevice::executeDrawList(const wsc::DrawList &drawList, int width, int height,
                                         int scissorOffsetX, int scissorOffsetY) const
{
    if (width <= 0 || height <= 0) {
        return false;
    }

    RenderContext context;
    context.setSize(width, height);
    context.setScissorOffset(scissorOffsetX, scissorOffsetY);

    for (const wsc::DrawPrimitive &prim : drawList) {
        ScissorState scissor;
        if (prim.scissorEnabled) {
            scissor.enabled = true;
            scissor.x = prim.scissorX;
            scissor.y = height - (prim.scissorY + prim.scissorHeight);
            scissor.width = prim.scissorWidth;
            scissor.height = prim.scissorHeight;
        }
        const DrawBlendMode blendMode = blendModeFromDrawListIndex(prim.blendMode);
        context.applyBlendMode(blendMode);

        if (prim.kind == wsc::DrawPrimitiveKind::SolidTriangles || prim.kind == wsc::DrawPrimitiveKind::GradientFill) {
            const std::size_t vertexCount = prim.positions.size() / 2u;
            if (vertexCount < 3 || (vertexCount % 3u) != 0u) {
                return false;
            }
            DrawPathData data;
            data.points.reserve(prim.positions.size());
            for (std::size_t i = 0; i < vertexCount; ++i) {
                data.points.push_back(fromNdcX(prim.positions[i * 2u + 0u], width));
                data.points.push_back(fromNdcY(prim.positions[i * 2u + 1u], height));
            }
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
                std::memcpy(data.gradientStopPositions, prim.gradientStopPositions, sizeof(data.gradientStopPositions));
                std::memcpy(data.gradientStopColors, prim.gradientStopColors, sizeof(data.gradientStopColors));
            }
            DrawPathProgram::getInstance()->draw(context, data);
        } else if (prim.kind == wsc::DrawPrimitiveKind::TexturedQuad) {
            if (!prim.texture || prim.positions.size() < 12u || prim.uvs.size() != prim.positions.size()) {
                return false;
            }
            DrawImageData data;
            data.imageResource = prim.texture;
            data.x = fromNdcX(prim.positions[0], width);
            data.y = fromNdcY(prim.positions[1], height);
            const float x1 = fromNdcX(prim.positions[4], width);
            const float y1 = fromNdcY(prim.positions[5], height);
            data.width = x1 - data.x;
            data.height = y1 - data.y;
            data.u0 = prim.uvs[0];
            data.v0 = prim.uvs[1];
            data.u1 = prim.uvs[4];
            data.v1 = prim.uvs[5];
            data.alpha = prim.layerAlpha;
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
            if (!prim.texture || !prim.texture->isValid()) {
                return false;
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
            return false;
        }
    }

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
    if (target.kind != BackendRenderTarget::Kind::OpenGLFramebuffer) {
        return false;
    }
    // Direct subsequent drawing into the host's framebuffer. WhatsCanvas draws
    // into the currently-bound FBO; offscreen passes (saveLayer/blur/clip) save
    // and restore GL_FRAMEBUFFER_BINDING, so this binding is honored throughout.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(target.glFramebuffer));
    if (target.width > 0 && target.height > 0) {
        glViewport(0, 0, target.width, target.height);
    }
    return true;
}

SharedImageResource OpenGLRenderDevice::renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                                      const OffscreenRenderRequest &request) const
{
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

    // Reuse the complete GL command path for offscreen replay. In particular,
    // this preserves arbitrary path clips on gradients, images, and text; the
    // backend-neutral DrawList encoder intentionally supports a smaller common
    // subset and is primarily used by non-GL device command execution.
    renderTarget->activate();
    RenderContext context;
    context.setSize(request.canvasWidth, request.canvasHeight);
    context.setScissorOffset(request.scissorOffsetX, request.scissorOffsetY);
    for (const std::unique_ptr<Command> &command : commands) {
        if (command) {
            command->execute(context);
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
                                                            const wsc::ImageFilter &filter) const
{
    const auto *input = dynamic_cast<const OpenGLImageResource *>(source.get());
    if (input == nullptr || !input->isValid() || width <= 0 || height <= 0
        || !filter.isValid() || filter.type() != wsc::ImageFilter::Type::Blur) {
        return {};
    }

    std::unique_ptr<IRenderTarget> targetA = createRenderTarget(width, height);
    std::unique_ptr<IRenderTarget> targetB = createRenderTarget(width, height);
    auto *glTargetA = dynamic_cast<OpenGLRenderTarget *>(targetA.get());
    auto *glTargetB = dynamic_cast<OpenGLRenderTarget *>(targetB.get());
    if (glTargetA == nullptr || glTargetB == nullptr
        || !glTargetA->isValid() || !glTargetB->isValid()) {
        return {};
    }
    const auto *imageA = dynamic_cast<const OpenGLImageResource *>(glTargetA->getImageResource().get());
    if (imageA == nullptr || !imageA->isValid()) {
        return {};
    }

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {};
    GLint previousProgram = 0;
    GLint previousVertexArray = 0;
    GLint previousActiveTexture = 0;
    GLint previousTexture0 = 0;
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);

    auto *blur = wsc::opengl::GaussianBlurProgram::getInstance();
    blur->initialize();
    const auto kernelX = wsc::render::computeGaussianKernel(filter.radiusX());
    const auto kernelY = wsc::render::computeGaussianKernel(filter.radiusY());
    const bool decal = filter.tileMode() == wsc::ImageFilter::TileMode::Decal;
    blur->blurImagePass(input->texture(), glTargetA->framebuffer(), width, height,
                        glm::vec2(1.0f / static_cast<float>(width), 0.0f), kernelX, decal);
    blur->blurImagePass(imageA->texture(), glTargetB->framebuffer(), width, height,
                        glm::vec2(0.0f, 1.0f / static_cast<float>(height)), kernelY, decal,
                        filter.saturation(), filter.brightness(), filter.contrast(),
                        filter.grain());

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glUseProgram(static_cast<GLuint>(previousProgram));
    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture0));
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

    return glTargetB->getImageResource();
}
