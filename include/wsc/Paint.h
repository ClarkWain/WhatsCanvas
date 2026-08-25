#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Color.h"
#include "Export.h"
#include "Font.h"

namespace wsc {

/// Copyable drawing-state value for geometry, text, images and layer compositing.
///
/// Canvas snapshots the Paint values needed by a recorded draw, so a Paint may
/// be changed or destroyed immediately after the call. Unless a method says
/// otherwise, dimensions are logical Canvas units. Defaults are anti-aliasing
/// on, opaque black fill/stroke, FILL style, 1-unit stroke, 16-unit text,
/// SRC_OVER blending and linear image sampling.
///
/// Minimal setup:
/// @code{.cpp}
/// wsc::Paint fillPaint;
/// fillPaint.setColor(wsc::Color(40, 120, 240));
/// fillPaint.setStyle(wsc::Paint::Style::FILL);
///
/// wsc::Paint strokePaint;
/// strokePaint.setColor(wsc::Color::BLACK);
/// strokePaint.setStyle(wsc::Paint::Style::STROKE);
/// strokePaint.setStrokeWidth(2.0f);
/// @endcode
///
/// Typical usage patterns:
/// - Solid color: `setColor()` / `setAlpha()` / `setFillColor()`
/// - Stroke: `setStyle(Paint::Style::STROKE)`, `setStrokeWidth()`, `setStrokeCap()`
/// - Gradient: `setLinearGradient()` or `setRadialGradient()`
/// - Text: set `setTextSize()`, `setTextAlign()`, `setTextBaseline()` and then
///   draw text with a Canvas method
/// - Layer blending: adjust `setBlendMode()` and image tinting through
///   `setColor()` / `setFillColor()` when needed
class WSC_API Paint
{
public:
    /// A single color stop for a gradient, at a normalized position in [0, 1].
    struct ColorStop
    {
        float position = 0.0f;
        Color color;

        ColorStop() = default;
        ColorStop(float position, const Color &color)
            : position(position), color(color)
        {
        }
    };

    /// Whole-run OpenType feature override. `tag` is the case-sensitive,
    /// four-byte OpenType tag (for example "liga", "kern", or "smcp").
    /// A value of 0 disables the feature, 1 normally enables it, and other
    /// values are passed through for features that select an alternate.
    /// Whether the override changes output depends on the selected font and
    /// shaping backend.
    struct FontFeature
    {
        std::string tag;
        std::uint32_t value = 1;

        FontFeature() = default;
        FontFeature(std::string featureTag, std::uint32_t featureValue = 1)
            : tag(std::move(featureTag)), value(featureValue)
        {
        }
    };

    /// Whether geometry is filled, stroked (outlined), or both.
    enum class Style
    {
        FILL,            ///< Fill the interior using fill color/shader.
        STROKE,          ///< Draw only the centered outline.
        FILL_AND_STROKE  ///< Fill first, then stroke.
    };

    /// Shape of the ends of an open stroked path.
    enum class StrokeCap
    {
        BUTT,   ///< End at the endpoint.
        ROUND,  ///< Add a half-circle cap.
        SQUARE  ///< Extend by half the stroke width.
    };

    /// How two stroked segments are joined at a corner.
    enum class StrokeJoin
    {
        MITER, ///< Extend edges until the miter limit is reached.
        ROUND, ///< Join with a circular arc.
        BEVEL  ///< Join with a straight clipped corner.
    };

    /// Active shader: a solid color or a linear/radial gradient.
    enum class ShaderType
    {
        SOLID,
        LINEAR_GRADIENT,
        RADIAL_GRADIENT
    };

    /// How a gradient/image repeats outside its defined range.
    enum class ShaderTileMode
    {
        CLAMP,  ///< Extend edge colors.
        REPEAT, ///< Repeat every unit interval.
        MIRROR, ///< Repeat with alternating direction.
        DECAL   ///< Transparent outside the defined range.
    };

    /// Horizontal alignment of text relative to its draw position.
    enum class TextAlign
    {
        LEFT,
        CENTER,
        RIGHT
    };

    /// Vertical anchor of text relative to its draw position.
    enum class TextBaseline
    {
        TOP,    ///< Y identifies the top of the text bounds (default).
        MIDDLE, ///< Y identifies the vertical middle of the text bounds.
        BOTTOM  ///< Y identifies the bottom of the text bounds.
    };

    /// Per-Paint text anti-aliasing mode override. `Default` inherits from the
    /// backend-wide mode chosen via `Canvas::setTextBackend`. `Grayscale` and
    /// `ClearType` request a specific mode for this Paint only; honoured by the
    /// native DirectWrite backend, ignored by the portable backend.
    enum class TextRenderMode
    {
        Default,
        Grayscale,
        ClearType
    };

    /// Porter-Duff and separable blend modes for compositing.
    enum class BlendMode
    {
        SRC_OVER,
        SRC,
        DST,
        CLEAR,
        SRC_IN,
        DST_IN,
        SRC_OUT,
        DST_OUT,
        SRC_ATOP,
        DST_ATOP,
        XOR,
        ADD,
        MULTIPLY,
        SCREEN
    };

    /// Texture sampling filter used when drawing images.
    enum class ImageSampling
    {
        LINEAR,        ///< Bilinear sampling; default.
        NEAREST,       ///< Nearest texel; useful for pixel art.
        MIPMAP_LINEAR  ///< Mipmap minification when mipmaps are available.
    };

    /// How an image repeats outside its source rectangle.
    enum class ImageTileMode
    {
        CLAMP,
        REPEAT,
        MIRROR,
        DECAL
    };

    /// Construct the documented default state. Paint is cheap to copy and move.
    Paint();

    Paint(const Paint &other);

    Paint &operator=(const Paint &other);

    Paint(Paint &&other) noexcept;

    Paint &operator=(Paint &&other) noexcept;

    ~Paint();

    /// Enable or disable geometry edge anti-aliasing. Text backends may apply
    /// their own rasterization mode.
    void setAntiAlias(bool aa);

    bool isAntiAlias() const;

    /// Set both fill and stroke color and clear any gradient shader. This color
    /// also tints images; use Color::WHITE to draw an image untinted.
    void setColor(const Color &color);

    void setColor(int r, int g, int b, int a = 255);

    void setColor(float r, float g, float b, float a = 1.0f);

    /// Set only the fill/image-tint color and clear any gradient shader.
    void setFillColor(const Color &color);

    /// Overall opacity multiplier (0-255 / 0.0-1.0) applied on top of the color.
    void setAlpha(int alpha);

    void setAlpha(float alpha);

    int getAlpha() const;

    float getAlphaF() const;

    Color getFillColor() const;

    /// Configure a linear gradient in local coordinates. Stop positions are
    /// clamped to [0,1], non-finite stops are removed and the remainder sorted.
    /// Replaces the current solid/gradient shader.
    void setLinearGradient(float startX, float startY, float endX, float endY,
                           const Color &startColor, const Color &endColor);

    void setLinearGradient(float startX, float startY, float endX, float endY,
                           const std::vector<ColorStop> &stops);

    /// Configure a radial gradient in local coordinates. Radius should be
    /// positive; stop normalization matches setLinearGradient().
    void setRadialGradient(float centerX, float centerY, float radius,
                           const Color &startColor, const Color &endColor);

    void setRadialGradient(float centerX, float centerY, float radius,
                           const std::vector<ColorStop> &stops);

    /// Remove any gradient and return to solid-color fill.
    void clearShader();

    ShaderType getShaderType() const;

    /// Control sampling outside the normalized gradient range.
    void setShaderTileMode(ShaderTileMode tileMode);

    ShaderTileMode getShaderTileMode() const;

    bool hasLinearGradient() const;

    bool hasRadialGradient() const;

    float getGradientStartX() const;

    float getGradientStartY() const;

    float getGradientEndX() const;

    float getGradientEndY() const;

    Color getGradientStartColor() const;

    Color getGradientEndColor() const;

    const std::vector<ColorStop> &getGradientStops() const;

    float getRadialCenterX() const;

    float getRadialCenterY() const;

    float getRadialRadius() const;

    Color getRadialStartColor() const;

    Color getRadialEndColor() const;

    /// Attach a drop shadow beneath supported geometry/text. Radius is blur
    /// reach and `(dx,dy)` is the local offset. Non-positive/transparent values
    /// effectively disable visible output; call clearShadowLayer() to reset.
    void setShadowLayer(float radius, float dx, float dy, const Color &color);

    void clearShadowLayer();

    bool hasShadowLayer() const;

    float getShadowRadius() const;

    float getShadowDx() const;

    float getShadowDy() const;

    Color getShadowColor() const;

    Color getColor() const;

    /// Centered stroke width in logical units; used when Style includes STROKE.
    /// Callers should provide a finite non-negative value.
    void setStrokeWidth(float width);

    float getStrokeWidth() const;

    /// Set the miter length/stroke-width limit; clamped to at least 1.
    void setStrokeMiterLimit(float limit);

    float getStrokeMiterLimit() const;

    /// Text size in logical units for drawText/drawTextBox. Must be positive.
    void setTextSize(float size);

    float getTextSize() const;

    /// Preferred UTF-8 font family. Resolution uses registered/provider/system
    /// faces and the Canvas fallback chain; an empty family selects defaults.
    void setFontFamily(const std::string &family);

    void setFont(const std::string &family);

    const std::string &getFontFamily() const;

    const std::string &getFont() const;

    bool hasFontFamily() const;

    void clearFontFamily();

    void clearFont();

    /// CSS/OpenType-style weight, clamped to [1,1000] (400 normal, 700 bold).
    void setFontWeight(int weight);

    int getFontWeight() const;

    void setFontSlant(FontSlant slant);

    FontSlant getFontSlant() const;

    /// Set or replace a whole-run OpenType variable-font axis override.
    /// Tags must contain exactly four bytes (for example `wght`, `wdth`, or
    /// `opsz`) and values must be finite. Paint overrides take precedence over
    /// coordinates carried by the resolved FontFace.
    void setFontVariation(const std::string &tag, float value);

    /// Remove all Paint-level variable-font overrides.
    void clearFontVariations();

    const std::vector<FontVariationCoordinate> &getFontVariations() const;

    /// Additional advance in logical units between shaped clusters. Non-finite
    /// input resets to zero.
    void setLetterSpacing(float spacing);

    float getLetterSpacing() const;

    /// Set horizontal and vertical anchoring of the `(x,y)` text draw position.
    void setTextAlign(TextAlign align);

    TextAlign getTextAlign() const;

    void setTextBaseline(TextBaseline baseline);

    TextBaseline getTextBaseline() const;

    /// BCP-47 locale (e.g. "en-US", "ja-JP") for locale-aware shaping and
    /// fallback. Honoured by portable HarfBuzz and native DirectWrite paths;
    /// empty by default.
    void setTextLocale(const std::string &locale);

    const std::string &getTextLocale() const;

    bool hasTextLocale() const;

    /// Set or update a whole-run OpenType feature override.
    ///
    /// There is no tag whitelist: any case-sensitive tag of exactly four bytes
    /// is forwarded to portable HarfBuzz shaping and native DirectWrite. An
    /// unsupported tag is normally ignored by the font/shaper, while tags of
    /// any other length are silently ignored here. Repeating a tag replaces its
    /// previous value. `value == 0` disables a feature, `value == 1` normally
    /// enables it, and other values are passed through for feature-specific
    /// alternate selection. The dependency-free simple shaper does not apply
    /// OpenType feature overrides.
    void setFontFeature(const std::string &tag, std::uint32_t value = 1);

    /// Remove every explicit feature override and restore font/shaper defaults.
    /// This differs from retaining a tag with value 0, which explicitly
    /// disables that feature.
    void clearFontFeatures();

    /// Return the active whole-run overrides, with at most one entry per tag.
    const std::vector<FontFeature> &getFontFeatures() const;

    /// Text decorations. Honoured by the native (DirectWrite) backend; off by
    /// default.
    void setUnderline(bool enabled);

    bool isUnderline() const;

    void setStrikethrough(bool enabled);

    bool isStrikethrough() const;

    /// Per-Paint text render mode override. Default = inherit backend setting.
    void setTextRenderMode(TextRenderMode mode);

    TextRenderMode getTextRenderMode() const;

    /// Compositing blend mode for subsequent draws.
    void setBlendMode(BlendMode blendMode);

    BlendMode getBlendMode() const;

    /// Set image minification/magnification sampling. MIPMAP_LINEAR falls back
    /// when the source has no mipmaps.
    void setImageSampling(ImageSampling sampling);

    ImageSampling getImageSampling() const;

    void setImageTileMode(ImageTileMode tileMode);

    ImageTileMode getImageTileMode() const;

    /// Round Path corners by a non-negative local radius before fill/stroke.
    /// Non-finite or negative input disables the effect.
    void setCornerPathEffect(float radius);

    void clearCornerPathEffect();

    bool hasCornerPathEffect() const;

    float getCornerPathEffectRadius() const;

    /// Dash a stroke using alternating positive on/off interval lengths. Invalid
    /// entries are removed; an odd valid list is duplicated to become even.
    /// Empty output disables dashing. Phase is in local units.
    void setDashPathEffect(const std::vector<float> &intervals, float phase = 0.0f);

    void clearDashPathEffect();

    bool hasDashPathEffect() const;

    const std::vector<float> &getDashIntervals() const;

    float getDashPhase() const;

    /// Apply a row-major 4x5 straight-RGBA color matrix. The fifth value in each
    /// row is an additive normalized-channel offset. Any non-finite element
    /// clears the filter.
    void setColorMatrix(const std::array<float, 20> &matrix);

    void clearColorMatrix();

    bool hasColorMatrix() const;

    const std::array<float, 20> &getColorMatrix() const;

    /// Set only the stroke color; does not change fill color or shader.
    void setStrokeColor(const Color &color);

    void setStrokeColor(int r, int g, int b, int a = 255);

    void setStrokeColor(float r, float g, float b, float a = 1.0f);

    Color getStrokeColor() const;

    /// Select fill/stroke evaluation for geometry and Path operations.
    void setStyle(Style style);

    Style getStyle() const;

    /// Configure cap/join geometry for stroked paths and lines.
    void setStrokeCap(StrokeCap cap);

    StrokeCap getStrokeCap() const;

    void setStrokeJoin(StrokeJoin join);

    StrokeJoin getStrokeJoin() const;

private:
    void setGradientStops(const std::vector<ColorStop> &stops);

    static const std::array<float, 20> kIdentityColorMatrix;

    Color color_ = Color::BLACK;
    Color strokeColor_ = Color::BLACK;
    int alpha_ = 255;
    float strokeWidth_ = 1.0f;
    float textSize_ = 16.0f;
    std::string fontFamily_;
    int fontWeight_ = 400;
    FontSlant fontSlant_ = FontSlant::NORMAL;
    std::vector<FontVariationCoordinate> fontVariations_;
    float letterSpacing_ = 0.0f;
    TextAlign textAlign_ = TextAlign::LEFT;
    TextBaseline textBaseline_ = TextBaseline::TOP;
    std::string textLocale_;
    std::vector<FontFeature> fontFeatures_;
    bool underline_ = false;
    bool strikethrough_ = false;
    TextRenderMode textRenderMode_ = TextRenderMode::Default;
    BlendMode blendMode_ = BlendMode::SRC_OVER;
    ImageSampling imageSampling_ = ImageSampling::LINEAR;
    ImageTileMode imageTileMode_ = ImageTileMode::CLAMP;
    float cornerPathEffectRadius_ = 0.0f;
    std::vector<float> dashIntervals_;
    float dashPhase_ = 0.0f;
    bool colorMatrixEnabled_ = false;
    std::array<float, 20> colorMatrix_ = kIdentityColorMatrix;
    bool antiAlias_ = true;
    Style style_ = Style::FILL;
    StrokeCap strokeCap_ = StrokeCap::BUTT;
    StrokeJoin strokeJoin_ = StrokeJoin::MITER;
    float strokeMiterLimit_ = 5.75959f;
    ShaderType shaderType_ = ShaderType::SOLID;
    ShaderTileMode shaderTileMode_ = ShaderTileMode::CLAMP;
    float gradientStartX_ = 0.0f;
    float gradientStartY_ = 0.0f;
    float gradientEndX_ = 1.0f;
    float gradientEndY_ = 0.0f;
    Color gradientStartColor_ = Color::BLACK;
    Color gradientEndColor_ = Color::WHITE;
    std::vector<ColorStop> gradientStops_;
    float radialCenterX_ = 0.0f;
    float radialCenterY_ = 0.0f;
    float radialRadius_ = 1.0f;
    Color radialStartColor_ = Color::WHITE;
    Color radialEndColor_ = Color::BLACK;
    bool shadowLayerEnabled_ = false;
    float shadowRadius_ = 0.0f;
    float shadowDx_ = 0.0f;
    float shadowDy_ = 0.0f;
    Color shadowColor_ = Color(0, 0, 0, 128);
};

} // namespace wsc
