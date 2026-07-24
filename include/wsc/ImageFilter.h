#pragma once

#include <algorithm>
#include <cmath>

#include "Export.h"

namespace wsc {

/// A pixel filter applied to a saved layer or to the content behind it.
///
/// The first public filter is a separable Gaussian blur. The value type is
/// intentionally backend-neutral so additional filter nodes can be added
/// without exposing GPU objects through the Canvas API.
class WSC_API ImageFilter
{
public:
    static constexpr float kMaxBlurRadius = 64.0f;
    static constexpr float kMaxBlurSigma = kMaxBlurRadius / 3.0f;

    enum class Type
    {
        None,
        Blur,
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
        filter.radiusX_ = std::clamp(radiusX, 0.0f, kMaxBlurRadius);
        filter.radiusY_ = std::clamp(radiusY, 0.0f, kMaxBlurRadius);
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

    /// Configure post-filter color adjustment. Saturation 0 produces grayscale;
    /// 1 leaves the channel unchanged. Brightness and contrast use 1 as neutral.
    ImageFilter &setColorAdjustment(float saturation, float brightness = 1.0f,
                                    float contrast = 1.0f)
    {
        saturation_ = std::clamp(saturation, 0.0f, 4.0f);
        brightness_ = std::clamp(brightness, 0.0f, 4.0f);
        contrast_ = std::clamp(contrast, 0.0f, 4.0f);
        return *this;
    }

    /// Add stable monochrome grain after filtering. Small values around
    /// 0.005-0.02 reduce visible banding without obscuring backdrop detail.
    ImageFilter &setGrain(float amount)
    {
        grain_ = std::clamp(amount, 0.0f, 0.25f);
        return *this;
    }

    Type type() const { return type_; }
    bool isValid() const
    {
        return type_ == Type::Blur && (radiusX_ > 0.0f || radiusY_ > 0.0f);
    }

    float radiusX() const { return radiusX_; }
    float radiusY() const { return radiusY_; }
    TileMode tileMode() const { return tileMode_; }
    float saturation() const { return saturation_; }
    float brightness() const { return brightness_; }
    float contrast() const { return contrast_; }
    float grain() const { return grain_; }
    bool hasColorAdjustment() const
    {
        return std::abs(saturation_ - 1.0f) > 1e-6f
            || std::abs(brightness_ - 1.0f) > 1e-6f
            || std::abs(contrast_ - 1.0f) > 1e-6f;
    }
    bool hasGrain() const { return grain_ > 1e-6f; }

private:
    Type type_ = Type::None;
    float radiusX_ = 0.0f;
    float radiusY_ = 0.0f;
    TileMode tileMode_ = TileMode::Clamp;
    float saturation_ = 1.0f;
    float brightness_ = 1.0f;
    float contrast_ = 1.0f;
    float grain_ = 0.0f;
};

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
