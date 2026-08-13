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

/// Drawing state container for fill, stroke, text and image options.
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

    /// Global OpenType shaping feature applied to a text run. Tags use the
    /// standard four-character form such as "liga", "kern", or "smcp".
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
        FILL,
        STROKE,
        FILL_AND_STROKE
    };

    /// Shape of the ends of an open stroked path.
    enum class StrokeCap
    {
        BUTT,
        ROUND,
        SQUARE
    };

    /// How two stroked segments are joined at a corner.
    enum class StrokeJoin
    {
        MITER,
        ROUND,
        BEVEL
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
        CLAMP,
        REPEAT,
        MIRROR,
        DECAL
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
        TOP,
        MIDDLE,
        BOTTOM
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
        LINEAR,
        NEAREST,
        MIPMAP_LINEAR
    };

    /// How an image repeats outside its source rectangle.
    enum class ImageTileMode
    {
        CLAMP,
        REPEAT,
        MIRROR,
        DECAL
    };

    Paint();
    Paint(const Paint &other);
    Paint &operator=(const Paint &other);
    Paint(Paint &&other) noexcept;
    Paint &operator=(Paint &&other) noexcept;
    ~Paint();

    /// Enable or disable anti-aliasing for edges drawn with this paint.
    void setAntiAlias(bool aa);
    bool isAntiAlias() const;

    /// Fill color (RGBA). Also used to tint images; use Color::WHITE to draw an
    /// image untinted. The default color is opaque black.
    void setColor(const Color &color);
    void setColor(int r, int g, int b, int a = 255);
    void setColor(float r, float g, float b, float a = 1.0f);
    void setFillColor(const Color &color);
    /// Overall opacity multiplier (0-255 / 0.0-1.0) applied on top of the color.
    void setAlpha(int alpha);
    void setAlpha(float alpha);
    int getAlpha() const;
    float getAlphaF() const;
    Color getFillColor() const;

    /// Configure a linear gradient fill between two points, with two colors or a
    /// list of color stops. Replaces any solid color/shader.
    void setLinearGradient(float startX, float startY, float endX, float endY,
                           const Color &startColor, const Color &endColor);
    void setLinearGradient(float startX, float startY, float endX, float endY,
                           const std::vector<ColorStop> &stops);
    /// Configure a radial gradient fill centered at a point, with two colors or
    /// a list of color stops.
    void setRadialGradient(float centerX, float centerY, float radius,
                           const Color &startColor, const Color &endColor);
    void setRadialGradient(float centerX, float centerY, float radius,
                           const std::vector<ColorStop> &stops);
    /// Remove any gradient and return to solid-color fill.
    void clearShader();
    ShaderType getShaderType() const;
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

    /// Attach a blurred drop shadow drawn beneath the shape (blur radius, offset
    /// and color). Applies to subsequent draws using this paint.
    void setShadowLayer(float radius, float dx, float dy, const Color &color);
    void clearShadowLayer();
    bool hasShadowLayer() const;
    float getShadowRadius() const;
    float getShadowDx() const;
    float getShadowDy() const;
    Color getShadowColor() const;

    Color getColor() const;
    /// Stroke (outline) width in pixels; used when Style includes STROKE.
    void setStrokeWidth(float width);
    float getStrokeWidth() const;
    void setStrokeMiterLimit(float limit);
    float getStrokeMiterLimit() const;

    /// Text size in pixels for drawText / drawTextBox.
    void setTextSize(float size);
    float getTextSize() const;
    /// Preferred font family name (must be registered on the Canvas).
    void setFontFamily(const std::string &family);
    void setFont(const std::string &family);
    const std::string &getFontFamily() const;
    const std::string &getFont() const;
    bool hasFontFamily() const;
    void clearFontFamily();
    void clearFont();
    void setFontWeight(int weight);
    int getFontWeight() const;
    void setFontSlant(FontSlant slant);
    FontSlant getFontSlant() const;
    void setLetterSpacing(float spacing);
    float getLetterSpacing() const;
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
    /// Set or update a global OpenType feature for portable HarfBuzz shaping.
    /// Invalid tags (anything other than four characters) are ignored.
    void setFontFeature(const std::string &tag, std::uint32_t value = 1);
    void clearFontFeatures();
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
    void setImageSampling(ImageSampling sampling);
    ImageSampling getImageSampling() const;
    void setImageTileMode(ImageTileMode tileMode);
    ImageTileMode getImageTileMode() const;
    /// Round the corners of stroked/filled paths by the given radius.
    void setCornerPathEffect(float radius);
    void clearCornerPathEffect();
    bool hasCornerPathEffect() const;
    float getCornerPathEffectRadius() const;
    /// Dash a stroked path using on/off interval lengths and a starting phase.
    void setDashPathEffect(const std::vector<float> &intervals, float phase = 0.0f);
    void clearDashPathEffect();
    bool hasDashPathEffect() const;
    const std::vector<float> &getDashIntervals() const;
    float getDashPhase() const;
    /// Apply a 4x5 color matrix to filter drawn colors (e.g. tint, grayscale).
    void setColorMatrix(const std::array<float, 20> &matrix);
    void clearColorMatrix();
    bool hasColorMatrix() const;
    const std::array<float, 20> &getColorMatrix() const;

    /// Separate stroke color (defaults to black); used when stroking.
    void setStrokeColor(const Color &color);
    void setStrokeColor(int r, int g, int b, int a = 255);
    void setStrokeColor(float r, float g, float b, float a = 1.0f);
    Color getStrokeColor() const;
    void setStyle(Style style);
    Style getStyle() const;
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
