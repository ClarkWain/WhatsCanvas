#pragma once

#include <array>
#include <string>
#include <vector>

#include "Color.h"
#include "Export.h"
#include "Font.h"

namespace wsc {

/// Drawing state container for fill, stroke, text and image options.
class WSC_API Paint
{
public:
    struct ColorStop
    {
        float position = 0.0f;
        Color color;

        ColorStop() = default;
        ColorStop(float position, const Color &color);
    };

    enum class Style
    {
        FILL,
        STROKE,
        FILL_AND_STROKE
    };

    enum class StrokeCap
    {
        BUTT,
        ROUND,
        SQUARE
    };

    enum class StrokeJoin
    {
        MITER,
        ROUND,
        BEVEL
    };

    enum class ShaderType
    {
        SOLID,
        LINEAR_GRADIENT,
        RADIAL_GRADIENT
    };

    enum class ShaderTileMode
    {
        CLAMP,
        REPEAT,
        MIRROR,
        DECAL
    };

    enum class TextAlign
    {
        LEFT,
        CENTER,
        RIGHT
    };

    enum class TextBaseline
    {
        TOP,
        MIDDLE,
        BOTTOM
    };

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

    enum class ImageSampling
    {
        LINEAR,
        NEAREST,
        MIPMAP_LINEAR
    };

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

    void setAntiAlias(bool aa);
    bool isAntiAlias() const;

    void setColor(const Color &color);
    void setColor(int r, int g, int b, int a = 255);
    void setColor(float r, float g, float b, float a = 1.0f);
    void setFillColor(const Color &color);
    void setAlpha(int alpha);
    void setAlpha(float alpha);
    int getAlpha() const;
    float getAlphaF() const;
    Color getFillColor() const;

    void setLinearGradient(float startX, float startY, float endX, float endY,
                           const Color &startColor, const Color &endColor);
    void setLinearGradient(float startX, float startY, float endX, float endY,
                           const std::vector<ColorStop> &stops);
    void setRadialGradient(float centerX, float centerY, float radius,
                           const Color &startColor, const Color &endColor);
    void setRadialGradient(float centerX, float centerY, float radius,
                           const std::vector<ColorStop> &stops);
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

    void setShadowLayer(float radius, float dx, float dy, const Color &color);
    void clearShadowLayer();
    bool hasShadowLayer() const;
    float getShadowRadius() const;
    float getShadowDx() const;
    float getShadowDy() const;
    Color getShadowColor() const;

    Color getColor() const;
    void setStrokeWidth(float width);
    float getStrokeWidth() const;
    void setStrokeMiterLimit(float limit);
    float getStrokeMiterLimit() const;

    void setTextSize(float size);
    float getTextSize() const;
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

    void setBlendMode(BlendMode blendMode);
    BlendMode getBlendMode() const;
    void setImageSampling(ImageSampling sampling);
    ImageSampling getImageSampling() const;
    void setImageTileMode(ImageTileMode tileMode);
    ImageTileMode getImageTileMode() const;
    void setCornerPathEffect(float radius);
    void clearCornerPathEffect();
    bool hasCornerPathEffect() const;
    float getCornerPathEffectRadius() const;
    void setDashPathEffect(const std::vector<float> &intervals, float phase = 0.0f);
    void clearDashPathEffect();
    bool hasDashPathEffect() const;
    const std::vector<float> &getDashIntervals() const;
    float getDashPhase() const;
    void setColorMatrix(const std::array<float, 20> &matrix);
    void clearColorMatrix();
    bool hasColorMatrix() const;
    const std::array<float, 20> &getColorMatrix() const;

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
    BlendMode blendMode_ = BlendMode::SRC_OVER;
    ImageSampling imageSampling_ = ImageSampling::LINEAR;
    ImageTileMode imageTileMode_ = ImageTileMode::CLAMP;
    float cornerPathEffectRadius_ = 0.0f;
    std::vector<float> dashIntervals_;
    float dashPhase_ = 0.0f;
    bool colorMatrixEnabled_ = false;
    std::array<float, 20> colorMatrix_ = kIdentityColorMatrix;
    bool antiAlias_ = false;
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
