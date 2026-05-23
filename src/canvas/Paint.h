#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <vector>
#include <array>

class Color
{
public:
    static const Color RED;
    static const Color GREEN;
    static const Color BLUE;
    static const Color WHITE;
    static const Color BLACK;
    static const Color YELLOW;
    static const Color CYAN;
    static const Color MAGENTA;

public:
    Color() : r_(0), g_(0), b_(0), a_(255) {}

    Color(int r, int g, int b, int a = 255) : r_(r), g_(g), b_(b), a_(a) {}

    static Color fromHex(const std::string &hex)
    {
        if (hex.size() != 9 && hex.size() != 7)
        {
            throw std::invalid_argument("Invalid hex color format");
        }

        int r, g, b, a = 255;
        std::istringstream(hex.substr(1, 2)) >> std::hex >> r;
        std::istringstream(hex.substr(3, 2)) >> std::hex >> g;
        std::istringstream(hex.substr(5, 2)) >> std::hex >> b;
        if (hex.size() == 9)
        {
            std::istringstream(hex.substr(7, 2)) >> std::hex >> a;
        }

        return Color(r, g, b, a);
    }

    int getR() const { return r_; }
    int getG() const { return g_; }
    int getB() const { return b_; }
    int getA() const { return a_; }

    float a() const { return a_ / 255.0f; }
    float r() const { return r_ / 255.0f; }
    float g() const { return g_ / 255.0f; }
    float b() const { return b_ / 255.0f; }

    void getNormalized(float* rgba) const
    {
        rgba[0] = r_ / 255.0f;
        rgba[1] = g_ / 255.0f;
        rgba[2] = b_ / 255.0f;
        rgba[3] = a_ / 255.0f;
    }

private:
    int r_, g_, b_, a_;
};

class Paint
{
public:
    struct ColorStop
    {
        float position = 0.0f;
        Color color;

        ColorStop() = default;
        ColorStop(float position, const Color &color) : position(position), color(color) {}
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

    Paint() = default;

    // Copy constructor
    Paint(const Paint &other)
    {
        color_ = other.color_;
        strokeColor_ = other.strokeColor_;
        alpha_ = other.alpha_;
        strokeWidth_ = other.strokeWidth_;
        textSize_ = other.textSize_;
        fontFamily_ = other.fontFamily_;
        letterSpacing_ = other.letterSpacing_;
        textAlign_ = other.textAlign_;
        textBaseline_ = other.textBaseline_;
        blendMode_ = other.blendMode_;
        imageSampling_ = other.imageSampling_;
        imageTileMode_ = other.imageTileMode_;
        cornerPathEffectRadius_ = other.cornerPathEffectRadius_;
        dashIntervals_ = other.dashIntervals_;
        dashPhase_ = other.dashPhase_;
        colorMatrixEnabled_ = other.colorMatrixEnabled_;
        colorMatrix_ = other.colorMatrix_;
        antiAlias_ = other.antiAlias_;
        style_ = other.style_;
        strokeCap_ = other.strokeCap_;
        strokeJoin_ = other.strokeJoin_;
        shaderType_ = other.shaderType_;
        shaderTileMode_ = other.shaderTileMode_;
        gradientStartX_ = other.gradientStartX_;
        gradientStartY_ = other.gradientStartY_;
        gradientEndX_ = other.gradientEndX_;
        gradientEndY_ = other.gradientEndY_;
        gradientStartColor_ = other.gradientStartColor_;
        gradientEndColor_ = other.gradientEndColor_;
        gradientStops_ = other.gradientStops_;
        radialCenterX_ = other.radialCenterX_;
        radialCenterY_ = other.radialCenterY_;
        radialRadius_ = other.radialRadius_;
        radialStartColor_ = other.radialStartColor_;
        radialEndColor_ = other.radialEndColor_;
        shadowLayerEnabled_ = other.shadowLayerEnabled_;
        shadowRadius_ = other.shadowRadius_;
        shadowDx_ = other.shadowDx_;
        shadowDy_ = other.shadowDy_;
        shadowColor_ = other.shadowColor_;
    }

    // Assignment operator
    Paint &operator=(const Paint &other)
    {
        if (this != &other)
        {
            color_ = other.color_;
            strokeColor_ = other.strokeColor_;
            alpha_ = other.alpha_;
            strokeWidth_ = other.strokeWidth_;
            textSize_ = other.textSize_;
            fontFamily_ = other.fontFamily_;
            letterSpacing_ = other.letterSpacing_;
            textAlign_ = other.textAlign_;
            textBaseline_ = other.textBaseline_;
            blendMode_ = other.blendMode_;
            imageSampling_ = other.imageSampling_;
            imageTileMode_ = other.imageTileMode_;
            cornerPathEffectRadius_ = other.cornerPathEffectRadius_;
            dashIntervals_ = other.dashIntervals_;
            dashPhase_ = other.dashPhase_;
            colorMatrixEnabled_ = other.colorMatrixEnabled_;
            colorMatrix_ = other.colorMatrix_;
            antiAlias_ = other.antiAlias_;
            style_ = other.style_;
            strokeCap_ = other.strokeCap_;
            strokeJoin_ = other.strokeJoin_;
            shaderType_ = other.shaderType_;
            shaderTileMode_ = other.shaderTileMode_;
            gradientStartX_ = other.gradientStartX_;
            gradientStartY_ = other.gradientStartY_;
            gradientEndX_ = other.gradientEndX_;
            gradientEndY_ = other.gradientEndY_;
            gradientStartColor_ = other.gradientStartColor_;
            gradientEndColor_ = other.gradientEndColor_;
            gradientStops_ = other.gradientStops_;
            radialCenterX_ = other.radialCenterX_;
            radialCenterY_ = other.radialCenterY_;
            radialRadius_ = other.radialRadius_;
            radialStartColor_ = other.radialStartColor_;
            radialEndColor_ = other.radialEndColor_;
            shadowLayerEnabled_ = other.shadowLayerEnabled_;
            shadowRadius_ = other.shadowRadius_;
            shadowDx_ = other.shadowDx_;
            shadowDy_ = other.shadowDy_;
            shadowColor_ = other.shadowColor_;
        }
        return *this;
    }

    // Move constructor
    Paint(Paint &&other) noexcept : color_(std::move(other.color_)), strokeColor_(std::move(other.strokeColor_)),
                                    alpha_(other.alpha_), strokeWidth_(other.strokeWidth_), textSize_(other.textSize_),
                                    fontFamily_(std::move(other.fontFamily_)), letterSpacing_(other.letterSpacing_),
                                    textAlign_(other.textAlign_), textBaseline_(other.textBaseline_),
                                    blendMode_(other.blendMode_),
                                    imageSampling_(other.imageSampling_),
                                    imageTileMode_(other.imageTileMode_),
                                    cornerPathEffectRadius_(other.cornerPathEffectRadius_),
                                    dashIntervals_(std::move(other.dashIntervals_)), dashPhase_(other.dashPhase_),
                                    colorMatrixEnabled_(other.colorMatrixEnabled_), colorMatrix_(other.colorMatrix_),
                                    antiAlias_(other.antiAlias_),
                                    style_(other.style_), strokeCap_(other.strokeCap_), strokeJoin_(other.strokeJoin_),
                                    shaderType_(other.shaderType_), shaderTileMode_(other.shaderTileMode_),
                                    gradientStartX_(other.gradientStartX_),
                                    gradientStartY_(other.gradientStartY_), gradientEndX_(other.gradientEndX_),
                                    gradientEndY_(other.gradientEndY_), gradientStartColor_(std::move(other.gradientStartColor_)),
                                    gradientEndColor_(std::move(other.gradientEndColor_)),
                                    gradientStops_(std::move(other.gradientStops_)),
                                    radialCenterX_(other.radialCenterX_), radialCenterY_(other.radialCenterY_),
                                    radialRadius_(other.radialRadius_), radialStartColor_(std::move(other.radialStartColor_)),
                                    radialEndColor_(std::move(other.radialEndColor_)),
                                    shadowLayerEnabled_(other.shadowLayerEnabled_), shadowRadius_(other.shadowRadius_),
                                    shadowDx_(other.shadowDx_), shadowDy_(other.shadowDy_),
                                    shadowColor_(std::move(other.shadowColor_))
    {
        other.strokeWidth_ = 0.0f;
        other.alpha_ = 255;
        other.textSize_ = 16.0f;
        other.fontFamily_.clear();
        other.letterSpacing_ = 0.0f;
        other.textAlign_ = TextAlign::LEFT;
        other.textBaseline_ = TextBaseline::TOP;
        other.blendMode_ = BlendMode::SRC_OVER;
        other.imageSampling_ = ImageSampling::LINEAR;
        other.imageTileMode_ = ImageTileMode::CLAMP;
        other.cornerPathEffectRadius_ = 0.0f;
        other.dashIntervals_.clear();
        other.dashPhase_ = 0.0f;
        other.clearColorMatrix();
        other.antiAlias_ = false;
        other.style_ = Style::FILL;
        other.strokeCap_ = StrokeCap::BUTT;
        other.strokeJoin_ = StrokeJoin::MITER;
        other.shaderType_ = ShaderType::SOLID;
        other.shaderTileMode_ = ShaderTileMode::CLAMP;
        other.gradientStops_.clear();
        other.shadowLayerEnabled_ = false;
    }

    // 设置抗锯齿
    void setAntiAlias(bool aa)
    {
        antiAlias_ = aa;
    }

    // 获取抗锯齿
    bool isAntiAlias() const
    {
        return antiAlias_;
    }

    // 设置颜色
    void setColor(const Color &color)
    {
        color_ = color;
        strokeColor_ = color;
        clearShader();
    }

    // 设置颜色
    void setColor(int r, int g, int b, int a = 255)
    {
        setColor(Color(r, g, b, a));
    }

    // 设置颜色
    void setColor(float r, float g, float b, float a = 1.0f)
    {
        setColor(Color(static_cast<int>(r * 255), static_cast<int>(g * 255),
                       static_cast<int>(b * 255), static_cast<int>(a * 255)));
    }

    void setFillColor(const Color &color)
    {
        color_ = color;
        clearShader();
    }

    void setAlpha(int alpha)
    {
        alpha_ = std::clamp(alpha, 0, 255);
    }

    void setAlpha(float alpha)
    {
        setAlpha(static_cast<int>(std::round(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)));
    }

    int getAlpha() const
    {
        return alpha_;
    }

    float getAlphaF() const
    {
        return static_cast<float>(alpha_) / 255.0f;
    }

    Color getFillColor() const
    {
        return color_;
    }

    void setLinearGradient(float startX, float startY, float endX, float endY,
                           const Color &startColor, const Color &endColor)
    {
        shaderType_ = ShaderType::LINEAR_GRADIENT;
        gradientStartX_ = startX;
        gradientStartY_ = startY;
        gradientEndX_ = endX;
        gradientEndY_ = endY;
        gradientStartColor_ = startColor;
        gradientEndColor_ = endColor;
        setGradientStops({ColorStop(0.0f, startColor), ColorStop(1.0f, endColor)});
    }

    void setLinearGradient(float startX, float startY, float endX, float endY,
                           const std::vector<ColorStop> &stops)
    {
        shaderType_ = ShaderType::LINEAR_GRADIENT;
        gradientStartX_ = startX;
        gradientStartY_ = startY;
        gradientEndX_ = endX;
        gradientEndY_ = endY;
        setGradientStops(stops);
        if (!gradientStops_.empty()) {
            gradientStartColor_ = gradientStops_.front().color;
            gradientEndColor_ = gradientStops_.back().color;
        }
    }

    void setRadialGradient(float centerX, float centerY, float radius,
                           const Color &startColor, const Color &endColor)
    {
        shaderType_ = ShaderType::RADIAL_GRADIENT;
        radialCenterX_ = centerX;
        radialCenterY_ = centerY;
        radialRadius_ = radius;
        radialStartColor_ = startColor;
        radialEndColor_ = endColor;
        setGradientStops({ColorStop(0.0f, startColor), ColorStop(1.0f, endColor)});
    }

    void setRadialGradient(float centerX, float centerY, float radius,
                           const std::vector<ColorStop> &stops)
    {
        shaderType_ = ShaderType::RADIAL_GRADIENT;
        radialCenterX_ = centerX;
        radialCenterY_ = centerY;
        radialRadius_ = radius;
        setGradientStops(stops);
        if (!gradientStops_.empty()) {
            radialStartColor_ = gradientStops_.front().color;
            radialEndColor_ = gradientStops_.back().color;
        }
    }

    void clearShader()
    {
        shaderType_ = ShaderType::SOLID;
    }

    ShaderType getShaderType() const
    {
        return shaderType_;
    }

    void setShaderTileMode(ShaderTileMode tileMode)
    {
        shaderTileMode_ = tileMode;
    }

    ShaderTileMode getShaderTileMode() const
    {
        return shaderTileMode_;
    }

    bool hasLinearGradient() const
    {
        return shaderType_ == ShaderType::LINEAR_GRADIENT;
    }

    bool hasRadialGradient() const
    {
        return shaderType_ == ShaderType::RADIAL_GRADIENT;
    }

    float getGradientStartX() const { return gradientStartX_; }
    float getGradientStartY() const { return gradientStartY_; }
    float getGradientEndX() const { return gradientEndX_; }
    float getGradientEndY() const { return gradientEndY_; }
    Color getGradientStartColor() const { return gradientStartColor_; }
    Color getGradientEndColor() const { return gradientEndColor_; }
    const std::vector<ColorStop>& getGradientStops() const { return gradientStops_; }
    float getRadialCenterX() const { return radialCenterX_; }
    float getRadialCenterY() const { return radialCenterY_; }
    float getRadialRadius() const { return radialRadius_; }
    Color getRadialStartColor() const { return radialStartColor_; }
    Color getRadialEndColor() const { return radialEndColor_; }

    void setShadowLayer(float radius, float dx, float dy, const Color &color)
    {
        shadowLayerEnabled_ = true;
        shadowRadius_ = radius;
        shadowDx_ = dx;
        shadowDy_ = dy;
        shadowColor_ = color;
    }

    void clearShadowLayer()
    {
        shadowLayerEnabled_ = false;
    }

    bool hasShadowLayer() const
    {
        return shadowLayerEnabled_ && shadowColor_.getA() > 0;
    }

    float getShadowRadius() const { return shadowRadius_; }
    float getShadowDx() const { return shadowDx_; }
    float getShadowDy() const { return shadowDy_; }
    Color getShadowColor() const { return shadowColor_; }

    // 获取颜色
    Color getColor() const
    {
        return color_;
    }

    // 设置线条宽度
    void setStrokeWidth(float width)
    {
        strokeWidth_ = width;
    }

    // 获取线条宽度
    float getStrokeWidth() const
    {
        return strokeWidth_;
    }

    void setTextSize(float size)
    {
        textSize_ = size;
    }

    float getTextSize() const
    {
        return textSize_;
    }

    void setFontFamily(const std::string &family)
    {
        fontFamily_ = family;
    }

    void setFont(const std::string &family)
    {
        setFontFamily(family);
    }

    const std::string& getFontFamily() const
    {
        return fontFamily_;
    }

    const std::string& getFont() const
    {
        return fontFamily_;
    }

    bool hasFontFamily() const
    {
        return !fontFamily_.empty();
    }

    void clearFontFamily()
    {
        fontFamily_.clear();
    }

    void clearFont()
    {
        clearFontFamily();
    }

    void setLetterSpacing(float spacing)
    {
        letterSpacing_ = std::isfinite(spacing) ? spacing : 0.0f;
    }

    float getLetterSpacing() const
    {
        return letterSpacing_;
    }

    void setTextAlign(TextAlign align)
    {
        textAlign_ = align;
    }

    TextAlign getTextAlign() const
    {
        return textAlign_;
    }

    void setTextBaseline(TextBaseline baseline)
    {
        textBaseline_ = baseline;
    }

    TextBaseline getTextBaseline() const
    {
        return textBaseline_;
    }

    void setBlendMode(BlendMode blendMode)
    {
        blendMode_ = blendMode;
    }

    BlendMode getBlendMode() const
    {
        return blendMode_;
    }

    void setImageSampling(ImageSampling sampling)
    {
        imageSampling_ = sampling;
    }

    ImageSampling getImageSampling() const
    {
        return imageSampling_;
    }

    void setImageTileMode(ImageTileMode tileMode)
    {
        imageTileMode_ = tileMode;
    }

    ImageTileMode getImageTileMode() const
    {
        return imageTileMode_;
    }

    void setCornerPathEffect(float radius)
    {
        cornerPathEffectRadius_ = std::isfinite(radius) ? std::max(0.0f, radius) : 0.0f;
    }

    void clearCornerPathEffect()
    {
        cornerPathEffectRadius_ = 0.0f;
    }

    bool hasCornerPathEffect() const
    {
        return cornerPathEffectRadius_ > 0.0f;
    }

    float getCornerPathEffectRadius() const
    {
        return cornerPathEffectRadius_;
    }

    void setDashPathEffect(const std::vector<float>& intervals, float phase = 0.0f)
    {
        dashIntervals_.clear();
        for (float interval : intervals) {
            if (std::isfinite(interval) && interval > 0.0f) {
                dashIntervals_.push_back(interval);
            }
        }

        if (dashIntervals_.empty()) {
            dashPhase_ = 0.0f;
            return;
        }

        if (dashIntervals_.size() % 2 == 1) {
            const size_t originalSize = dashIntervals_.size();
            dashIntervals_.reserve(originalSize * 2);
            for (size_t i = 0; i < originalSize; ++i) {
                dashIntervals_.push_back(dashIntervals_[i]);
            }
        }

        dashPhase_ = std::isfinite(phase) ? phase : 0.0f;
    }

    void clearDashPathEffect()
    {
        dashIntervals_.clear();
        dashPhase_ = 0.0f;
    }

    bool hasDashPathEffect() const
    {
        return !dashIntervals_.empty();
    }

    const std::vector<float>& getDashIntervals() const
    {
        return dashIntervals_;
    }

    float getDashPhase() const
    {
        return dashPhase_;
    }

    void setColorMatrix(const std::array<float, 20> &matrix)
    {
        for (float value : matrix) {
            if (!std::isfinite(value)) {
                clearColorMatrix();
                return;
            }
        }
        colorMatrix_ = matrix;
        colorMatrixEnabled_ = true;
    }

    void clearColorMatrix()
    {
        colorMatrixEnabled_ = false;
        colorMatrix_ = kIdentityColorMatrix;
    }

    bool hasColorMatrix() const
    {
        return colorMatrixEnabled_;
    }

    const std::array<float, 20>& getColorMatrix() const
    {
        return colorMatrix_;
    }

    // 设置线条颜色
    void setStrokeColor(const Color &color)
    {
        strokeColor_ = color;
    }

    void setStrokeColor(int r, int g, int b, int a = 255)
    {
        strokeColor_ = Color(r, g, b, a);
    }

    void setStrokeColor(float r, float g, float b, float a = 1.0f)
    {
        strokeColor_ = Color(static_cast<int>(r * 255), static_cast<int>(g * 255),
                             static_cast<int>(b * 255), static_cast<int>(a * 255));
    }

    // 获取线条颜色
    Color getStrokeColor() const
    {
        return strokeColor_;
    }

    // 设置样式
    void setStyle(Style style)
    {
        style_ = style;
    }

    // 获取样式
    Style getStyle() const
    {
        return style_;
    }

    // 设置线条端点样式
    void setStrokeCap(StrokeCap cap)
    {
        strokeCap_ = cap;
    }

    // 获取线条端点样式
    StrokeCap getStrokeCap() const
    {
        return strokeCap_;
    }

    void setStrokeJoin(StrokeJoin join)
    {
        strokeJoin_ = join;
    }

    StrokeJoin getStrokeJoin() const
    {
        return strokeJoin_;
    }

private:
    void setGradientStops(const std::vector<ColorStop> &stops)
    {
        gradientStops_.clear();
        gradientStops_.reserve(stops.size());
        for (const auto &stop : stops) {
            if (!std::isfinite(stop.position)) {
                continue;
            }
            gradientStops_.emplace_back(std::clamp(stop.position, 0.0f, 1.0f), stop.color);
        }

        std::sort(gradientStops_.begin(), gradientStops_.end(), [](const ColorStop &a, const ColorStop &b) {
            return a.position < b.position;
        });
    }

    Color color_ = Color::BLACK;
    Color strokeColor_ = Color::BLACK;
    int alpha_ = 255;
    float strokeWidth_ = 1.0f;
    float textSize_ = 16.0f;
    std::string fontFamily_;
    float letterSpacing_ = 0.0f;
    TextAlign textAlign_ = TextAlign::LEFT;
    TextBaseline textBaseline_ = TextBaseline::TOP;
    BlendMode blendMode_ = BlendMode::SRC_OVER;
    ImageSampling imageSampling_ = ImageSampling::LINEAR;
    ImageTileMode imageTileMode_ = ImageTileMode::CLAMP;
    float cornerPathEffectRadius_ = 0.0f;
    std::vector<float> dashIntervals_;
    float dashPhase_ = 0.0f;
    inline static const std::array<float, 20> kIdentityColorMatrix = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f
    };
    bool colorMatrixEnabled_ = false;
    std::array<float, 20> colorMatrix_ = kIdentityColorMatrix;
    bool antiAlias_ = false;
    Style style_ = Style::FILL;
    StrokeCap strokeCap_ = StrokeCap::BUTT;
    StrokeJoin strokeJoin_ = StrokeJoin::MITER;
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
