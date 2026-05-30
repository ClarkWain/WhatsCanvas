#pragma once

#include <glad/glad.h>

#include "render/RenderTypes.h"

class RenderContext
{
public:
    RenderContext() = default;
    void setSize(int width, int height)
    {
        this->width = width;
        this->height = height;
        centerX = width / 2.0f;
        centerY = height / 2.0f;
    }

    int getWidth() const { return width; }
    int getHeight() const { return height; }

    void setScissorOffset(int x, int y)
    {
        scissorOffsetX = x;
        scissorOffsetY = y;
    }

    int getScissorOffsetX() const { return scissorOffsetX; }
    int getScissorOffsetY() const { return scissorOffsetY; }

    float getCenterX() const { return centerX; }
    float getCenterY() const { return centerY; }

    void applyClipState(const ScissorState &scissor, const ClipMaskState &clipMask) const;

private:
    bool isClipMaskCurrent(std::uint64_t key) const
    {
        return hasClipMaskKey_ && lastClipMaskKey_ == key;
    }

    void rememberClipMask(std::uint64_t key) const
    {
        hasClipMaskKey_ = true;
        lastClipMaskKey_ = key;
    }

    void clearClipMask() const
    {
        hasClipMaskKey_ = false;
        lastClipMaskKey_ = 0;
    }

public:

    void applyScissorState(const ScissorState &scissor) const
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

    void applyBlendMode(DrawBlendMode mode) const
    {
        if (!blendEnabled_) {
            glEnable(GL_BLEND);
            blendEnabled_ = true;
        }

        if (hasBlendMode_ && lastBlendMode_ == mode) {
            return;
        }

        glBlendEquation(GL_FUNC_ADD);

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
    }

    void bindImageHandle(ImageResourceHandle texture) const
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

    void bindImageResource(const SharedImageResource &imageResource, DrawImageSampling sampling,
                           DrawImageTileMode tileMode, bool mipmapsReady) const
    {
        if (!imageResource || !imageResource->isValid()) {
            return;
        }

        imageResource->bind(*this);

        const GLint textureWrap = tileMode == DrawImageTileMode::Repeat
            ? GL_REPEAT
            : (tileMode == DrawImageTileMode::Mirror ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE);

        GLint minFilter = GL_LINEAR;
        GLint magFilter = GL_LINEAR;
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

    void resetRenderState() const
    {
        if (scissorEnabled_) {
            glDisable(GL_SCISSOR_TEST);
            scissorEnabled_ = false;
        }
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        clearClipMask();
        hasScissorRect_ = false;
        hasBlendMode_ = false;
        hasBoundTexture_ = false;
        hasTextureState_ = false;
        generatedMipmapsForBoundTexture_ = false;
    }

private:
    int width = 0;
    int height = 0;
    int scissorOffsetX = 0;
    int scissorOffsetY = 0;
    float centerX = 0;
    float centerY = 0;
    mutable bool blendEnabled_ = false;
    mutable bool hasBlendMode_ = false;
    mutable DrawBlendMode lastBlendMode_ = DrawBlendMode::SrcOver;
    mutable bool scissorEnabled_ = false;
    mutable bool hasScissorRect_ = false;
    mutable int lastScissorX_ = 0;
    mutable int lastScissorY_ = 0;
    mutable int lastScissorWidth_ = 0;
    mutable int lastScissorHeight_ = 0;
    mutable bool hasBoundTexture_ = false;
    mutable ImageResourceHandle boundTexture_;
    mutable bool hasTextureState_ = false;
    mutable GLint lastTextureWrap_ = GL_CLAMP_TO_EDGE;
    mutable GLint lastTextureMinFilter_ = GL_LINEAR;
    mutable GLint lastTextureMagFilter_ = GL_LINEAR;
    mutable bool generatedMipmapsForBoundTexture_ = false;
    mutable bool hasClipMaskKey_ = false;
    mutable std::uint64_t lastClipMaskKey_ = 0;
};