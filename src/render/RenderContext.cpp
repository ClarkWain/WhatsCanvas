#include "render/RenderContext.h"

#include <algorithm>

#include <glad/glad.h>

#include "command/DrawData.h"
#include "command/DrawPath.h"
#include "opengl/ClipCoverageProgram.h"

namespace {

// Texture unit reserved for the anti-aliased clip coverage mask. Content uses
// unit 0 and the gradient texel buffer uses unit 1, so unit 3 stays free.
constexpr int kClipMaskTextureUnit = 3;

std::uint64_t makeClipStateKey(const ClipMaskState &clipMask, const ScissorState &scissor,
                               int width, int height)
{
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;
    std::uint64_t hash = clipMask.fingerprint == 0 ? 1469598103934665603ull : clipMask.fingerprint;
    const std::uint64_t scissorValues[7] = {
        scissor.enabled ? 1ull : 0ull,
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(scissor.x)),
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(scissor.y)),
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(scissor.width)),
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(scissor.height)),
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(width)),
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(height))
    };

    for (std::uint64_t value : scissorValues) {
        hash ^= value;
        hash *= kFnvPrime;
    }

    return hash;
}

} // namespace

RenderContext::RenderContext() = default;

void RenderContext::setSize(int width, int height)
{
    this->width = width;
    this->height = height;
    centerX = width / 2.0f;
    centerY = height / 2.0f;
}

int RenderContext::getWidth() const
{
    return width;
}

int RenderContext::getHeight() const
{
    return height;
}

void RenderContext::setScissorOffset(int x, int y)
{
    scissorOffsetX = x;
    scissorOffsetY = y;
}

int RenderContext::getScissorOffsetX() const
{
    return scissorOffsetX;
}

int RenderContext::getScissorOffsetY() const
{
    return scissorOffsetY;
}

float RenderContext::getCenterX() const
{
    return centerX;
}

float RenderContext::getCenterY() const
{
    return centerY;
}

bool RenderContext::isClipMaskCurrent(std::uint64_t key) const
{
    return hasClipMaskKey_ && lastClipMaskKey_ == key;
}

void RenderContext::rememberClipMask(std::uint64_t key) const
{
    hasClipMaskKey_ = true;
    lastClipMaskKey_ = key;
}

void RenderContext::clearClipMask() const
{
    hasClipMaskKey_ = false;
    lastClipMaskKey_ = 0;
}

void RenderContext::bindClipMaskTexture(unsigned int texture) const
{
    glActiveTexture(GL_TEXTURE0 + kClipMaskTextureUnit);
    glBindTexture(GL_TEXTURE_2D, texture);
    glActiveTexture(GL_TEXTURE0);
}

int RenderContext::clipMaskTextureUnit() const
{
    return kClipMaskTextureUnit;
}

void RenderContext::applyClipState(const ScissorState &scissor, const ClipMaskState &clipMask) const
{
    if (!clipMask.hasPaths()) {
        clipMaskActive_ = false;
        if (hasClipMaskKey_) {
            clearClipMask();
        }
        if (!scissor.enabled && !scissorEnabled_) {
            hasScissorRect_ = false;
            return;
        }
        applyScissorState(scissor);
        return;
    }

    const std::uint64_t clipKey = makeClipStateKey(clipMask, scissor, width, height);
    if (isClipMaskCurrent(clipKey)) {
        clipMaskActive_ = true;
        applyScissorState(scissor);
        return;
    }

    // Save the GL state that the downstream draw expects to still be current
    // when applyClipState returns. This must happen BEFORE any clip-coverage
    // machinery runs — ClipCoverageProgram::initialize() ends with
    // glBindVertexArray(0), and later steps switch GLProgram, so reads after
    // that point capture the polluted state instead of the caller's.
    // Downstream draw programs (see DrawPathProgram::draw) cache their own
    // active-program/VAO bookkeeping and skip glUseProgram/glBindVertexArray
    // when they think the value is unchanged. Without this save/restore, the
    // second path draw in a batch runs against GL program 0 / VAO 0 and
    // triggers GL_INVALID_OPERATION on the first uniform upload and
    // vertex-attribute setup, silently producing empty output.
    GLint previousProgram = 0;
    GLint previousVertexArray = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);

    auto *program = wsc::opengl::ClipCoverageProgram::getInstance();
    program->initialize();
    if (const GLuint cached = program->findCachedMask(clipKey, clipMask, width, height)) {
        bindClipMaskTexture(cached);
        applyScissorState(scissor);
        clipMaskActive_ = true;
        rememberClipMask(clipKey);
        // The cache lookup still ran ClipCoverageProgram::initialize() which
        // may have unbound the caller's VAO; restore it so the next draw
        // does not blow up on glVertexAttribPointer.
        glUseProgram(static_cast<GLuint>(previousProgram));
        glBindVertexArray(static_cast<GLuint>(previousVertexArray));
        return;
    }
    std::size_t validClipCount = 0;
    const SharedClipMaskResource *singleClip = nullptr;
    for (const auto &clipResource : clipMask.resources) {
        if (clipResource && clipResource->isValid()) {
            ++validClipCount;
            singleClip = &clipResource;
        }
    }
    const bool singleClipFastPath = validClipCount == 1u;
    if (width <= 0 || height <= 0
        || validClipCount == 0u
        || !program->ensureTargets(
            width, height, !singleClipFastPath)) {
        clipMaskActive_ = false;
        clearClipMask();
        applyScissorState(scissor);
        // Same VAO/program restore as above — ensureTargets can leave state
        // altered even when it returns false.
        glUseProgram(static_cast<GLuint>(previousProgram));
        glBindVertexArray(static_cast<GLuint>(previousVertexArray));
        return;
    }

    // Build the anti-aliased clip coverage mask off-screen. Save and restore the
    // caller's framebuffer/viewport so the subsequent draw targets the frame.
    // Repeated glGet calls between draws can drain the driver's command queue.
    // Clip passes restore this exact target; keep it until state is invalidated.
    if (!hasFramebufferState_) {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer_);
        glGetIntegerv(GL_VIEWPORT, viewport_);
        hasFramebufferState_ = true;
    }
    const GLint previousFramebuffer = framebuffer_;
    const GLint previousViewport[4] = {viewport_[0], viewport_[1], viewport_[2], viewport_[3]};

    if (singleClipFastPath) {
        program->beginSingleClipLayer(width, height);
        (*singleClip)->apply(*this, scissor, 0);
    } else {
        program->beginAccumulator(width, height);
        for (const auto &clipResource : clipMask.resources) {
            if (!clipResource || !clipResource->isValid()) {
                continue;
            }
            program->beginClipLayer(width, height);
            clipResource->apply(*this, scissor, 0);
            program->multiplyLayerIntoAccumulator(width, height);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glBlendEquation(GL_FUNC_ADD);
    const GLuint completedMask = program->accumulatorTexture();
    program->cacheAccumulator(clipKey, clipMask, width, height);
    bindClipMaskTexture(completedMask);

    // The off-screen passes changed GL state directly, so drop the cached state
    // and re-establish scissor; the following applyBlendMode/draw re-issue theirs.
    resetRenderState();
    framebuffer_ = previousFramebuffer;
    std::copy_n(previousViewport, 4, viewport_);
    hasFramebufferState_ = true;
    // Restore the GLProgram and VAO that the caller had bound. See the
    // save-block above for the full explanation; the downstream draw
    // program's cached "active program / VAO" bookkeeping is only valid if
    // the real GL state matches what it thinks it left behind.
    glUseProgram(static_cast<GLuint>(previousProgram));
    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    applyScissorState(scissor);
    clipMaskActive_ = true;
    rememberClipMask(clipKey);
}

void RenderContext::applyScissorState(const ScissorState &scissor) const
{
    const int resolvedX = scissor.x + scissorOffsetX;
    const int resolvedY = scissor.y + scissorOffsetY;
    if (scissor.enabled) {
        if (!scissorEnabled_) {
            glEnable(GL_SCISSOR_TEST);
            scissorEnabled_ = true;
        }

        if (!hasScissorRect_ || lastScissorX_ != resolvedX || lastScissorY_ != resolvedY ||
            lastScissorWidth_ != scissor.width || lastScissorHeight_ != scissor.height) {
            glScissor(resolvedX, resolvedY, scissor.width, scissor.height);
            lastScissorX_ = resolvedX;
            lastScissorY_ = resolvedY;
            lastScissorWidth_ = scissor.width;
            lastScissorHeight_ = scissor.height;
            hasScissorRect_ = true;
        }
    } else {
        if (scissorEnabled_) {
            glDisable(GL_SCISSOR_TEST);
            scissorEnabled_ = false;
        }
        hasScissorRect_ = false;
    }
}

void RenderContext::applyBlendMode(DrawBlendMode mode) const
{
    if (!blendEnabled_) {
        glEnable(GL_BLEND);
        blendEnabled_ = true;
    }

    if (hasBlendMode_ && lastBlendMode_ == mode) {
        clearTypeBlendModeActive_ = false;
        return;
    }

    ensureAddBlendEquation();

    switch (mode) {
    case DrawBlendMode::SrcOver:
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case DrawBlendMode::Src:
        glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
        break;
    case DrawBlendMode::Dst:
        glBlendFuncSeparate(GL_ZERO, GL_ONE, GL_ZERO, GL_ONE);
        break;
    case DrawBlendMode::Clear:
        glBlendFuncSeparate(GL_ZERO, GL_ZERO, GL_ZERO, GL_ZERO);
        break;
    case DrawBlendMode::SrcIn:
        glBlendFuncSeparate(GL_DST_ALPHA, GL_ZERO, GL_DST_ALPHA, GL_ZERO);
        break;
    case DrawBlendMode::DstIn:
        glBlendFuncSeparate(GL_ZERO, GL_SRC_ALPHA, GL_ZERO, GL_SRC_ALPHA);
        break;
    case DrawBlendMode::SrcOut:
        glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_ZERO, GL_ONE_MINUS_DST_ALPHA, GL_ZERO);
        break;
    case DrawBlendMode::DstOut:
        glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case DrawBlendMode::SrcAtop:
        glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case DrawBlendMode::DstAtop:
        glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_SRC_ALPHA);
        break;
    case DrawBlendMode::Xor:
        glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case DrawBlendMode::Add:
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
        break;
    case DrawBlendMode::Multiply:
        glBlendFuncSeparate(GL_DST_COLOR, GL_ZERO, GL_DST_ALPHA, GL_ZERO);
        break;
    case DrawBlendMode::Screen:
        glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }

    lastBlendMode_ = mode;
    hasBlendMode_ = true;
    clearTypeBlendModeActive_ = false;
}

void RenderContext::ensureAddBlendEquation() const
{
    if (!addBlendEquationActive_) {
        glBlendEquation(GL_FUNC_ADD);
        addBlendEquationActive_ = true;
    }
}

bool RenderContext::applyClearTypeBlendMode() const
{
#if defined(WHATSCANVAS_OPENGL_ES)
    return false;
#else
    if (maxDualSourceDrawBuffers_ < 0) {
        glGetIntegerv(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS, &maxDualSourceDrawBuffers_);
    }
    if (maxDualSourceDrawBuffers_ < 1) {
        return false;
    }
    if (!blendEnabled_) {
        glEnable(GL_BLEND);
        blendEnabled_ = true;
    }
    // source0.rgb is foreground * RGB coverage. source1.rgb is RGB coverage,
    // giving: out = source0 + destination * (1 - source1) per channel.
    ensureAddBlendEquation();
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC1_COLOR, GL_ZERO, GL_ONE);
    hasBlendMode_ = false;
    clearTypeBlendModeActive_ = true;
    return true;
#endif
}

void RenderContext::bindImageHandle(ImageResourceHandle texture) const
{
    if (!texture.isValid()) {
        return;
    }

    glActiveTexture(GL_TEXTURE0);
    if (!hasBoundTexture_ || boundTexture_.value != texture.value) {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture.value));
        boundTexture_ = texture;
        hasBoundTexture_ = true;
        hasTextureState_ = false;
        generatedMipmapsForBoundTexture_ = false;
    }
}

void RenderContext::bindImageResource(const SharedImageResource &imageResource, DrawImageSampling sampling,
                                      DrawImageTileMode tileMode, bool mipmapsReady) const
{
    if (!imageResource || !imageResource->isValid()) {
        return;
    }

    imageResource->bind(*this);

    const int textureWrap = tileMode == DrawImageTileMode::Repeat
        ? GL_REPEAT
        : (tileMode == DrawImageTileMode::Mirror ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE);

    int minFilter = GL_LINEAR;
    int magFilter = GL_LINEAR;
    if (sampling == DrawImageSampling::Nearest) {
        minFilter = GL_NEAREST;
        magFilter = GL_NEAREST;
    } else if (sampling == DrawImageSampling::MipmapLinear) {
        if (!mipmapsReady && !generatedMipmapsForBoundTexture_) {
            glGenerateMipmap(GL_TEXTURE_2D);
            generatedMipmapsForBoundTexture_ = true;
        }
        minFilter = GL_LINEAR_MIPMAP_LINEAR;
        magFilter = GL_LINEAR;
    }

    if (!hasTextureState_ || lastTextureWrap_ != textureWrap ||
        lastTextureMinFilter_ != minFilter || lastTextureMagFilter_ != magFilter) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, textureWrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, textureWrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
        lastTextureWrap_ = textureWrap;
        lastTextureMinFilter_ = minFilter;
        lastTextureMagFilter_ = magFilter;
        hasTextureState_ = true;
    }
}

void RenderContext::invalidateImageBinding() const
{
    hasBoundTexture_ = false;
    boundTexture_ = {};
    hasTextureState_ = false;
    generatedMipmapsForBoundTexture_ = false;
}

void RenderContext::resetRenderState() const
{
    hasFramebufferState_ = false;
    if (scissorEnabled_) {
        glDisable(GL_SCISSOR_TEST);
        scissorEnabled_ = false;
    }
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    clearClipMask();
    clipMaskActive_ = false;
    hasScissorRect_ = false;
    addBlendEquationActive_ = false;
    hasBlendMode_ = false;
    clearTypeBlendModeActive_ = false;
    hasBoundTexture_ = false;
    hasTextureState_ = false;
    generatedMipmapsForBoundTexture_ = false;
}
