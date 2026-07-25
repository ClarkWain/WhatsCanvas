#pragma once

#include <algorithm>
#include <cmath>

#include "Color.h"
#include "Export.h"

namespace wsc {

/// A pixel filter applied to a saved layer or to the content behind it.
///
/// Blur and inner shadow are represented as a backend-neutral value, keeping
/// GPU objects out of the Canvas API.
class WSC_API ImageFilter
{
public:
    static constexpr float kMaxBlurRadius = 64.0f;
    static constexpr float kMaxBlurSigma = kMaxBlurRadius / 3.0f;
    static constexpr float kMaxShadowOffset = 256.0f;

    enum class Type
    {
        None,
        Blur,
        InnerShadow,
    };

    enum class TileMode
    {
        Clamp,
        Decal,
    };

    ImageFilter() = default;

    /// Create a Gaussian blur whose radii are measured in filter-target pixels.
    /// Values are clamped to kMaxBlurRadius for backend parity.
    static ImageFilter blur(float radiusX, float radiusY, TileMode tileMode = TileMode::Clamp)
    {
        ImageFilter filter;
        filter.type_ = Type::Blur;
        filter.radiusX_ = clampRadius(radiusX);
        filter.radiusY_ = clampRadius(radiusY);
        filter.tileMode_ = tileMode;
        return filter;
    }

    /// Create an isotropic Gaussian blur.
    static ImageFilter blur(float radius, TileMode tileMode = TileMode::Clamp)
    {
        return blur(radius, radius, tileMode);
    }

    /// Create a Gaussian blur using standard deviation, matching the parameter
    /// convention used by APIs such as Skia. The sampled reach is three sigma.
    static ImageFilter blurSigma(float sigmaX, float sigmaY,
                                 TileMode tileMode = TileMode::Clamp)
    {
        return blur(sigmaX * 3.0f, sigmaY * 3.0f, tileMode);
    }

    static ImageFilter blurSigma(float sigma, TileMode tileMode = TileMode::Clamp)
    {
        return blurSigma(sigma, sigma, tileMode);
    }

    /// Create an inset shadow from the filtered layer's alpha. The source
    /// pixels and alpha are preserved; the shadow is composited only inside
    /// the source silhouette. Offsets follow inset-shadow convention:
    /// positive X shades the left edge and positive Y shades the top edge.
    static ImageFilter innerShadow(float radiusX, float radiusY,
                                   float offsetX, float offsetY,
                                   const Color &color)
    {
        ImageFilter filter;
        filter.type_ = Type::InnerShadow;
        filter.radiusX_ = clampRadius(radiusX);
        filter.radiusY_ = clampRadius(radiusY);
        // InnerShadow reuses the post-blur payload slots. This keeps the
        // exported ImageFilter value layout ABI-compatible with the original
        // blur-only type.
        filter.saturation_ = clampOffset(offsetX);
        filter.brightness_ = clampOffset(offsetY);
        filter.contrast_ =
            static_cast<float>(color.getR() * 256 + color.getG());
        filter.grain_ =
            static_cast<float>(color.getB() * 256 + color.getA());
        filter.tileMode_ = TileMode::Decal;
        return filter;
    }

    static ImageFilter innerShadow(float radius, float offsetX, float offsetY,
                                   const Color &color)
    {
        return innerShadow(radius, radius, offsetX, offsetY, color);
    }

    static ImageFilter innerShadowSigma(float sigmaX, float sigmaY,
                                        float offsetX, float offsetY,
                                        const Color &color)
    {
        return innerShadow(sigmaX * 3.0f, sigmaY * 3.0f,
                           offsetX, offsetY, color);
    }

    static ImageFilter innerShadowSigma(float sigma, float offsetX, float offsetY,
                                        const Color &color)
    {
        return innerShadowSigma(sigma, sigma, offsetX, offsetY, color);
    }

    /// Create a practical frosted-glass backdrop filter. Color adjustment is
    /// applied after the blur, leaving layer content drawn above it untouched.
    static ImageFilter frostedGlass(float blurSigma,
                                    float saturation = 1.18f,
                                    float brightness = 1.04f,
                                    float contrast = 1.02f,
                                    float grain = 0.012f,
                                    TileMode tileMode = TileMode::Clamp)
    {
        ImageFilter filter = ImageFilter::blurSigma(blurSigma, tileMode);
        filter.setColorAdjustment(saturation, brightness, contrast);
        filter.setGrain(grain);
        return filter;
    }

    /// Configure post-blur color adjustment. Saturation 0 produces grayscale;
    /// 1 leaves the channel unchanged. Brightness and contrast use 1 as neutral.
    /// This blur-only modifier is ignored by other filter types.
    ImageFilter &setColorAdjustment(float saturation, float brightness = 1.0f,
                                    float contrast = 1.0f)
    {
        if (type_ != Type::Blur) {
            return *this;
        }
        saturation_ = std::clamp(saturation, 0.0f, 4.0f);
        brightness_ = std::clamp(brightness, 0.0f, 4.0f);
        contrast_ = std::clamp(contrast, 0.0f, 4.0f);
        return *this;
    }

    /// Add stable monochrome grain after a blur. Small values around 0.005-0.02
    /// reduce visible banding. This is ignored by other filter types.
    ImageFilter &setGrain(float amount)
    {
        if (type_ != Type::Blur) {
            return *this;
        }
        grain_ = std::clamp(amount, 0.0f, 0.25f);
        return *this;
    }

    Type type() const { return type_; }
    bool isValid() const
    {
        if (type_ == Type::Blur) {
            return radiusX_ > 0.0f || radiusY_ > 0.0f;
        }
        return type_ == Type::InnerShadow && shadowColor().getA() > 0;
    }

    float radiusX() const { return radiusX_; }
    float radiusY() const { return radiusY_; }
    float offsetX() const { return type_ == Type::InnerShadow ? saturation_ : 0.0f; }
    float offsetY() const { return type_ == Type::InnerShadow ? brightness_ : 0.0f; }
    Color shadowColor() const
    {
        if (type_ != Type::InnerShadow) {
            return Color(0, 0, 0, 0);
        }
        const int rg = static_cast<int>(contrast_);
        const int ba = static_cast<int>(grain_);
        return Color(rg / 256, rg % 256, ba / 256, ba % 256);
    }
    TileMode tileMode() const { return tileMode_; }
    float saturation() const { return type_ == Type::Blur ? saturation_ : 1.0f; }
    float brightness() const { return type_ == Type::Blur ? brightness_ : 1.0f; }
    float contrast() const { return type_ == Type::Blur ? contrast_ : 1.0f; }
    float grain() const { return type_ == Type::Blur ? grain_ : 0.0f; }
    bool hasColorAdjustment() const
    {
        return type_ == Type::Blur
            && (std::abs(saturation_ - 1.0f) > 1e-6f
            || std::abs(brightness_ - 1.0f) > 1e-6f
            || std::abs(contrast_ - 1.0f) > 1e-6f);
    }
    bool hasGrain() const { return type_ == Type::Blur && grain_ > 1e-6f; }

    /// Conservative source sampling expansion required by this filter.
    float samplingOutset() const
    {
        if (type_ == Type::InnerShadow) {
            return std::max(radiusX_ + std::abs(offsetX()),
                            radiusY_ + std::abs(offsetY()));
        }
        return type_ == Type::Blur ? std::max(radiusX_, radiusY_) : 0.0f;
    }

    /// Expansion of the visible filtered result beyond the original layer.
    /// Inner shadows remain clipped inside the source silhouette.
    float outputOutset() const
    {
        return type_ == Type::Blur ? std::max(radiusX_, radiusY_) : 0.0f;
    }

private:
    static float clampRadius(float radius)
    {
        return std::clamp(std::isnan(radius) ? 0.0f : radius,
                          0.0f, kMaxBlurRadius);
    }

    static float clampOffset(float offset)
    {
        return std::clamp(std::isnan(offset) ? 0.0f : offset,
                          -kMaxShadowOffset, kMaxShadowOffset);
    }

    Type type_ = Type::None;
    float radiusX_ = 0.0f;
    float radiusY_ = 0.0f;
    TileMode tileMode_ = TileMode::Clamp;
    float saturation_ = 1.0f;
    float brightness_ = 1.0f;
    float contrast_ = 1.0f;
    float grain_ = 0.0f;
};

static_assert(sizeof(ImageFilter) == 32,
              "ImageFilter must retain its public blur-only ABI size");

/// Optional effects attached to a saveLayer operation.
///
/// `imageFilter` processes the layer's own content. `backdropFilter` processes
/// the pixels already drawn behind the layer before the layer content is added.
class WSC_API LayerOptions
{
public:
    LayerOptions &setImageFilter(const ImageFilter &filter)
    {
        imageFilter_ = filter;
        return *this;
    }

    LayerOptions &setBackdropFilter(const ImageFilter &filter)
    {
        backdropFilter_ = filter;
        return *this;
    }

    const ImageFilter &imageFilter() const { return imageFilter_; }
    const ImageFilter &backdropFilter() const { return backdropFilter_; }
    bool hasImageFilter() const { return imageFilter_.isValid(); }
    bool hasBackdropFilter() const { return backdropFilter_.isValid(); }

private:
    ImageFilter imageFilter_;
    ImageFilter backdropFilter_;
};

} // namespace wsc
