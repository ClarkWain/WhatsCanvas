#pragma once

#include "render/RenderTypes.h"

class RenderContext
{
public:
    RenderContext();

    void setSize(int width, int height);
    int getWidth() const;
    int getHeight() const;

    void setScissorOffset(int x, int y);
    int getScissorOffsetX() const;
    int getScissorOffsetY() const;

    float getCenterX() const;
    float getCenterY() const;

    void applyClipState(const ScissorState &scissor, const ClipMaskState &clipMask) const;
    void applyScissorState(const ScissorState &scissor) const;
    void applyBlendMode(DrawBlendMode mode) const;
    /// Configure dual-source RGB compositing for an LCD/ClearType mask.
    /// Returns false when the current GL implementation cannot provide the
    /// required dual-source blend output, allowing callers to use normal
    /// grayscale/SrcOver fallback instead.
    bool applyClearTypeBlendMode() const;
    bool isClearTypeBlendModeActive() const { return clearTypeBlendModeActive_; }
    void bindImageHandle(ImageResourceHandle texture) const;
    void bindImageResource(const SharedImageResource &imageResource, DrawImageSampling sampling,
                           DrawImageTileMode tileMode, bool mipmapsReady) const;
    void resetRenderState() const;

    /// Whether an anti-aliased clip coverage mask is currently bound. Draw
    /// programs sample it and multiply their output alpha by the coverage.
    bool isClipMaskActive() const { return clipMaskActive_; }
    /// Texture unit holding the clip coverage mask (valid when active).
    int clipMaskTextureUnit() const;

private:
    bool isClipMaskCurrent(std::uint64_t key) const;
    void rememberClipMask(std::uint64_t key) const;
    void clearClipMask() const;
    void bindClipMaskTexture(unsigned int texture) const;

    int width = 0;
    int height = 0;
    int scissorOffsetX = 0;
    int scissorOffsetY = 0;
    float centerX = 0;
    float centerY = 0;
    mutable bool blendEnabled_ = false;
    mutable bool hasBlendMode_ = false;
    mutable DrawBlendMode lastBlendMode_ = DrawBlendMode::SrcOver;
    mutable bool clearTypeBlendModeActive_ = false;
    mutable int maxDualSourceDrawBuffers_ = -1;
    mutable bool scissorEnabled_ = false;
    mutable bool hasScissorRect_ = false;
    mutable int lastScissorX_ = 0;
    mutable int lastScissorY_ = 0;
    mutable int lastScissorWidth_ = 0;
    mutable int lastScissorHeight_ = 0;
    mutable bool hasBoundTexture_ = false;
    mutable ImageResourceHandle boundTexture_;
    mutable bool hasTextureState_ = false;
    mutable int lastTextureWrap_ = 0;
    mutable int lastTextureMinFilter_ = 0;
    mutable int lastTextureMagFilter_ = 0;
    mutable bool generatedMipmapsForBoundTexture_ = false;
    mutable bool hasClipMaskKey_ = false;
    mutable std::uint64_t lastClipMaskKey_ = 0;
    mutable bool clipMaskActive_ = false;
};
