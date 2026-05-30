#include "render/OpenGLRenderDevice.h"

#include <algorithm>

#include <glad/glad.h>

#include "command/DrawCommand.h"
#include "opengl/GLTextureUtils.h"
#include "render/IRenderer.h"
#include "render/IRenderTarget.h"
#include "render/RenderContext.h"

namespace {

int g_renderDeviceBackendRefCount = 0;

class OpenGLImageResource final : public ImageResource
{
public:
    explicit OpenGLImageResource(ImageResourceHandle handle)
        : handle_(handle)
    {
    }

    ~OpenGLImageResource() override
    {
        if (handle_.isValid()) {
            prismcanvas::opengl::destroyTexture(handle_);
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

private:
    ImageResourceHandle handle_;
};

class OpenGLClipMaskResource final : public ClipMaskResource
{
public:
    explicit OpenGLClipMaskResource(const ClipMaskPath &maskPath)
        : points_(maskPath.points),
          transform_(maskPath.transform)
    {
    }

    bool isValid() const override
    {
        return !points_.empty();
    }

    void apply(const RenderContext &context, const ScissorState &scissor, std::size_t clipIndex) const override
    {
        if (!isValid()) {
            return;
        }

        DrawPathData clipData;
        clipData.points = points_;
        clipData.color[0] = 1.0f;
        clipData.color[1] = 1.0f;
        clipData.color[2] = 1.0f;
        clipData.color[3] = 1.0f;
        clipData.drawMode = PathDrawMode::Fill;
        clipData.capStyle = PathCapStyle::Bevel;
        clipData.transform = transform_;
        clipData.scissor = scissor;
        clipData.blendMode = DrawBlendMode::Src;
        glStencilFunc(GL_EQUAL, static_cast<GLint>(clipIndex), 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
        DrawPathProgram::getInstance()->draw(context, clipData);
    }

private:
    std::vector<float> points_;
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
    }

    ~OpenGLRenderTarget() override
    {
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

    bool begin(const OffscreenRenderRequest &request) override
    {
        if (!isValid()) {
            return false;
        }

        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer_);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture_);
        glGetIntegerv(GL_VIEWPORT, previousViewport_);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor_);

        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
        glViewport(request.viewportX, request.viewportY, request.canvasWidth, request.canvasHeight);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        active_ = true;
        return true;
    }

    void end() override
    {
        if (!active_) {
            return;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer_));
        glViewport(previousViewport_[0], previousViewport_[1], previousViewport_[2], previousViewport_[3]);
        glClearColor(previousClearColor_[0], previousClearColor_[1], previousClearColor_[2], previousClearColor_[3]);
        glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(previousTexture_));
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        active_ = false;
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
    bool active_ = false;
    GLint previousFramebuffer_ = 0;
    GLint previousTexture_ = 0;
    GLint previousViewport_[4] = {0, 0, 0, 0};
    GLfloat previousClearColor_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

SharedImageResource createSharedOpenGLImageResource(ImageResourceHandle handle)
{
    if (!handle.isValid()) {
        return {};
    }

    return std::make_shared<OpenGLImageResource>(handle);
}

void initializeSharedRenderBackend()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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
}

void executeCommandList(const std::vector<std::unique_ptr<Command>> &commands, int width, int height,
                        int scissorOffsetX = 0, int scissorOffsetY = 0)
{
    RenderContext context;
    context.setSize(width, height);
    context.setScissorOffset(scissorOffsetX, scissorOffsetY);
    for (const auto &command : commands) {
        command->execute(context);
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
    if (!prismcanvas::opengl::createRenderTargetTexture(width, height, framebuffer, stencilRenderbuffer, texture)) {
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
    return createSharedOpenGLImageResource(prismcanvas::opengl::createTextureRGBA(width, height, pixels));
}

SharedImageResource OpenGLRenderDevice::createImageResourceFromImageData(int width, int height, int channels,
                                                                         const unsigned char *pixels,
                                                                         bool generateMipmaps) const
{
    return createSharedOpenGLImageResource(
        prismcanvas::opengl::createTextureFromImageData(width, height, channels, pixels, generateMipmaps));
}

SharedImageResource OpenGLRenderDevice::renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                                      const OffscreenRenderRequest &request) const
{
    if (commands.empty() || request.canvasWidth <= 0 || request.canvasHeight <= 0 ||
        request.targetWidth <= 0 || request.targetHeight <= 0) {
        return {};
    }

    std::unique_ptr<IRenderTarget> renderTarget = createRenderTarget(request.targetWidth, request.targetHeight);
    if (!renderTarget || !renderTarget->begin(request)) {
        return {};
    }

    executeCommandList(commands, request.canvasWidth, request.canvasHeight,
                       request.scissorOffsetX, request.scissorOffsetY);
    renderTarget->end();
    return renderTarget->getImageResource();
}