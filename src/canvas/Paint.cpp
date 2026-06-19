#include "Paint.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace wsc {

const Color Color::RED = Color(255, 0, 0);
const Color Color::GREEN = Color(0, 255, 0);
const Color Color::BLUE = Color(0, 0, 255);
const Color Color::WHITE = Color(255, 255, 255);
const Color Color::BLACK = Color(0, 0, 0);
const Color Color::YELLOW = Color(255, 255, 0);
const Color Color::CYAN = Color(0, 255, 255);
const Color Color::MAGENTA = Color(255, 0, 255);

Color::Color()
    : r_(0), g_(0), b_(0), a_(255)
{
}

Color::Color(int r, int g, int b, int a)
    : r_(r), g_(g), b_(b), a_(a)
{
}

Color Color::fromHex(const std::string &hex)
{
    if (hex.size() != 9 && hex.size() != 7) {
        throw std::invalid_argument("Invalid hex color format");
    }

    int r = 0;
    int g = 0;
    int b = 0;
    int a = 255;
    std::istringstream(hex.substr(1, 2)) >> std::hex >> r;
    std::istringstream(hex.substr(3, 2)) >> std::hex >> g;
    std::istringstream(hex.substr(5, 2)) >> std::hex >> b;
    if (hex.size() == 9) {
        std::istringstream(hex.substr(7, 2)) >> std::hex >> a;
    }

    return Color(r, g, b, a);
}

int Color::getR() const { return r_; }
int Color::getG() const { return g_; }
int Color::getB() const { return b_; }
int Color::getA() const { return a_; }
float Color::a() const { return a_ / 255.0f; }
float Color::r() const { return r_ / 255.0f; }
float Color::g() const { return g_ / 255.0f; }
float Color::b() const { return b_ / 255.0f; }

void Color::getNormalized(float *rgba) const
{
    rgba[0] = r_ / 255.0f;
    rgba[1] = g_ / 255.0f;
    rgba[2] = b_ / 255.0f;
    rgba[3] = a_ / 255.0f;
}

const std::array<float, 20> Paint::kIdentityColorMatrix = {
    1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f, 0.0f
};

Paint::ColorStop::ColorStop(float position, const Color &color)
    : position(position), color(color)
{
}

Paint::Paint() = default;
Paint::~Paint() = default;

Paint::Paint(const Paint &other) = default;
Paint &Paint::operator=(const Paint &other) = default;
Paint::Paint(Paint &&other) noexcept = default;
Paint &Paint::operator=(Paint &&other) noexcept = default;

void Paint::setAntiAlias(bool aa) { antiAlias_ = aa; }
bool Paint::isAntiAlias() const { return antiAlias_; }

void Paint::setColor(const Color &color)
{
    color_ = color;
    strokeColor_ = color;
    clearShader();
}

void Paint::setColor(int r, int g, int b, int a)
{
    setColor(Color(r, g, b, a));
}

void Paint::setColor(float r, float g, float b, float a)
{
    setColor(Color(static_cast<int>(r * 255), static_cast<int>(g * 255),
                   static_cast<int>(b * 255), static_cast<int>(a * 255)));
}

void Paint::setFillColor(const Color &color)
{
    color_ = color;
    clearShader();
}

void Paint::setAlpha(int alpha)
{
    alpha_ = std::clamp(alpha, 0, 255);
}

void Paint::setAlpha(float alpha)
{
    setAlpha(static_cast<int>(std::round(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)));
}

int Paint::getAlpha() const { return alpha_; }
float Paint::getAlphaF() const { return static_cast<float>(alpha_) / 255.0f; }
Color Paint::getFillColor() const { return color_; }

void Paint::setLinearGradient(float startX, float startY, float endX, float endY,
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

void Paint::setLinearGradient(float startX, float startY, float endX, float endY,
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

void Paint::setRadialGradient(float centerX, float centerY, float radius,
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

void Paint::setRadialGradient(float centerX, float centerY, float radius,
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

void Paint::clearShader() { shaderType_ = ShaderType::SOLID; }
Paint::ShaderType Paint::getShaderType() const { return shaderType_; }
void Paint::setShaderTileMode(ShaderTileMode tileMode) { shaderTileMode_ = tileMode; }
Paint::ShaderTileMode Paint::getShaderTileMode() const { return shaderTileMode_; }
bool Paint::hasLinearGradient() const { return shaderType_ == ShaderType::LINEAR_GRADIENT; }
bool Paint::hasRadialGradient() const { return shaderType_ == ShaderType::RADIAL_GRADIENT; }
float Paint::getGradientStartX() const { return gradientStartX_; }
float Paint::getGradientStartY() const { return gradientStartY_; }
float Paint::getGradientEndX() const { return gradientEndX_; }
float Paint::getGradientEndY() const { return gradientEndY_; }
Color Paint::getGradientStartColor() const { return gradientStartColor_; }
Color Paint::getGradientEndColor() const { return gradientEndColor_; }
const std::vector<Paint::ColorStop> &Paint::getGradientStops() const { return gradientStops_; }
float Paint::getRadialCenterX() const { return radialCenterX_; }
float Paint::getRadialCenterY() const { return radialCenterY_; }
float Paint::getRadialRadius() const { return radialRadius_; }
Color Paint::getRadialStartColor() const { return radialStartColor_; }
Color Paint::getRadialEndColor() const { return radialEndColor_; }

void Paint::setShadowLayer(float radius, float dx, float dy, const Color &color)
{
    shadowLayerEnabled_ = true;
    shadowRadius_ = radius;
    shadowDx_ = dx;
    shadowDy_ = dy;
    shadowColor_ = color;
}

void Paint::clearShadowLayer() { shadowLayerEnabled_ = false; }
bool Paint::hasShadowLayer() const { return shadowLayerEnabled_ && shadowColor_.getA() > 0; }
float Paint::getShadowRadius() const { return shadowRadius_; }
float Paint::getShadowDx() const { return shadowDx_; }
float Paint::getShadowDy() const { return shadowDy_; }
Color Paint::getShadowColor() const { return shadowColor_; }

Color Paint::getColor() const { return color_; }
void Paint::setStrokeWidth(float width) { strokeWidth_ = width; }
float Paint::getStrokeWidth() const { return strokeWidth_; }
void Paint::setTextSize(float size) { textSize_ = size; }
float Paint::getTextSize() const { return textSize_; }
void Paint::setFontFamily(const std::string &family) { fontFamily_ = family; }
void Paint::setFont(const std::string &family) { setFontFamily(family); }
const std::string &Paint::getFontFamily() const { return fontFamily_; }
const std::string &Paint::getFont() const { return fontFamily_; }
bool Paint::hasFontFamily() const { return !fontFamily_.empty(); }
void Paint::clearFontFamily() { fontFamily_.clear(); }
void Paint::clearFont() { clearFontFamily(); }

void Paint::setLetterSpacing(float spacing)
{
    letterSpacing_ = std::isfinite(spacing) ? spacing : 0.0f;
}

float Paint::getLetterSpacing() const { return letterSpacing_; }
void Paint::setTextAlign(TextAlign align) { textAlign_ = align; }
Paint::TextAlign Paint::getTextAlign() const { return textAlign_; }
void Paint::setTextBaseline(TextBaseline baseline) { textBaseline_ = baseline; }
Paint::TextBaseline Paint::getTextBaseline() const { return textBaseline_; }
void Paint::setBlendMode(BlendMode blendMode) { blendMode_ = blendMode; }
Paint::BlendMode Paint::getBlendMode() const { return blendMode_; }
void Paint::setImageSampling(ImageSampling sampling) { imageSampling_ = sampling; }
Paint::ImageSampling Paint::getImageSampling() const { return imageSampling_; }
void Paint::setImageTileMode(ImageTileMode tileMode) { imageTileMode_ = tileMode; }
Paint::ImageTileMode Paint::getImageTileMode() const { return imageTileMode_; }

void Paint::setCornerPathEffect(float radius)
{
    cornerPathEffectRadius_ = std::isfinite(radius) ? std::max(0.0f, radius) : 0.0f;
}

void Paint::clearCornerPathEffect() { cornerPathEffectRadius_ = 0.0f; }
bool Paint::hasCornerPathEffect() const { return cornerPathEffectRadius_ > 0.0f; }
float Paint::getCornerPathEffectRadius() const { return cornerPathEffectRadius_; }

void Paint::setDashPathEffect(const std::vector<float> &intervals, float phase)
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

void Paint::clearDashPathEffect()
{
    dashIntervals_.clear();
    dashPhase_ = 0.0f;
}

bool Paint::hasDashPathEffect() const { return !dashIntervals_.empty(); }
const std::vector<float> &Paint::getDashIntervals() const { return dashIntervals_; }
float Paint::getDashPhase() const { return dashPhase_; }

void Paint::setColorMatrix(const std::array<float, 20> &matrix)
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

void Paint::clearColorMatrix()
{
    colorMatrixEnabled_ = false;
    colorMatrix_ = kIdentityColorMatrix;
}

bool Paint::hasColorMatrix() const { return colorMatrixEnabled_; }
const std::array<float, 20> &Paint::getColorMatrix() const { return colorMatrix_; }

void Paint::setStrokeColor(const Color &color) { strokeColor_ = color; }
void Paint::setStrokeColor(int r, int g, int b, int a) { strokeColor_ = Color(r, g, b, a); }
void Paint::setStrokeColor(float r, float g, float b, float a)
{
    strokeColor_ = Color(static_cast<int>(r * 255), static_cast<int>(g * 255),
                         static_cast<int>(b * 255), static_cast<int>(a * 255));
}

Color Paint::getStrokeColor() const { return strokeColor_; }
void Paint::setStyle(Style style) { style_ = style; }
Paint::Style Paint::getStyle() const { return style_; }
void Paint::setStrokeCap(StrokeCap cap) { strokeCap_ = cap; }
Paint::StrokeCap Paint::getStrokeCap() const { return strokeCap_; }
void Paint::setStrokeJoin(StrokeJoin join) { strokeJoin_ = join; }
Paint::StrokeJoin Paint::getStrokeJoin() const { return strokeJoin_; }

void Paint::setGradientStops(const std::vector<ColorStop> &stops)
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

} // namespace wsc
