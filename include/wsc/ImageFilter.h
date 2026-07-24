#pragma once

#include <algorithm>

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

    Type type() const { return type_; }
    bool isValid() const
    {
        return type_ == Type::Blur && (radiusX_ > 0.0f || radiusY_ > 0.0f);
    }

    float radiusX() const { return radiusX_; }
    float radiusY() const { return radiusY_; }
    TileMode tileMode() const { return tileMode_; }

private:
    Type type_ = Type::None;
    float radiusX_ = 0.0f;
    float radiusY_ = 0.0f;
    TileMode tileMode_ = TileMode::Clamp;
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
