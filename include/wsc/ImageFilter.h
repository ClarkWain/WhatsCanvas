#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "Color.h"
#include "Export.h"

namespace wsc {

/// A pixel filter applied to a saved layer or to the content behind it.
///
/// Blur and inner shadow are represented as a backend-neutral value, keeping
/// GPU objects out of the Canvas API. Values are cheap to copy and are snapped
/// by saveLayer(). Invalid/zero filters are ignored.
class WSC_API ImageFilter
{
public:
    static constexpr float kMaxBlurRadius = 64.0f;
    static constexpr float kMaxBlurSigma = kMaxBlurRadius / 3.0f;
    static constexpr float kMaxShadowOffset = 256.0f;

    /// Active filter operation.
    enum class Type
    {
        None,
        Blur,
        InnerShadow,
    };

    /// Sampling outside the layer bounds during blur.
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

    /// Create a Gaussian blur using the standard-deviation convention common
    /// to graphics APIs. The sampled reach is three sigma.
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

    /// Query normalized/clamped filter properties.
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

/// An ordered, bounded image-filter pipeline.
///
/// Blur and inner shadow retain the compact ImageFilter value ABI. Generic
/// color-matrix and offset nodes live in this explicit chain so their larger
/// payloads do not add pointers or ownership to every ImageFilter value. A
/// chain stores at most kMaxNodes; invalid nodes and overflow appends are
/// ignored and leave the chain unchanged.
class WSC_API ImageFilterChain
{
public:
    static constexpr std::size_t kMaxNodes = 8;

    enum class NodeType
    {
        ImageFilter,
        ColorMatrix,
        Offset,
    };

    struct Node
    {
        NodeType type = NodeType::ImageFilter;
        ImageFilter imageFilter;
        std::array<float, 20> colorMatrix = {
            1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        };
        float offsetX = 0.0f;
        float offsetY = 0.0f;
    };

    ImageFilterChain() = default;

    explicit ImageFilterChain(const ImageFilter &filter)
    {
        append(filter);
    }

    /// Append one valid filter when capacity remains; otherwise no-op.
    ImageFilterChain &append(const ImageFilter &filter)
    {
        if (!filter.isValid() || nodes_.size() >= kMaxNodes) {
            return *this;
        }
        Node node;
        node.type = NodeType::ImageFilter;
        node.imageFilter = filter;
        nodes_.push_back(std::move(node));
        return *this;
    }

    /// Append a row-major 4x5 RGBA matrix. The fifth element of every row is
    /// an additive normalized-channel offset.
    ImageFilterChain &appendColorMatrix(
        const std::array<float, 20> &matrix)
    {
        if (nodes_.size() >= kMaxNodes
            || !std::all_of(
                matrix.begin(), matrix.end(),
                [](float value) { return std::isfinite(value); })) {
            return *this;
        }
        Node node;
        node.type = NodeType::ColorMatrix;
        node.colorMatrix = matrix;
        nodes_.push_back(std::move(node));
        return *this;
    }

    /// Translate the filtered image. Pixels exposed outside the source bounds
    /// are transparent (Decal semantics).
    ImageFilterChain &appendOffset(float dx, float dy)
    {
        if (nodes_.size() >= kMaxNodes
            || !std::isfinite(dx) || !std::isfinite(dy)) {
            return *this;
        }
        Node node;
        node.type = NodeType::Offset;
        node.offsetX = std::clamp(
            dx, -ImageFilter::kMaxShadowOffset,
            ImageFilter::kMaxShadowOffset);
        node.offsetY = std::clamp(
            dy, -ImageFilter::kMaxShadowOffset,
            ImageFilter::kMaxShadowOffset);
        nodes_.push_back(std::move(node));
        return *this;
    }

    std::size_t size() const { return nodes_.size(); }

    bool empty() const { return nodes_.empty(); }

    bool isValid() const { return !nodes_.empty(); }

    /// Borrow a node. `index` must be less than size().
    const Node &operator[](std::size_t index) const
    {
        return nodes_[index];
    }

    float samplingOutset() const
    {
        float outset = 0.0f;
        for (std::size_t index = 0; index < nodes_.size(); ++index) {
            const Node &node = nodes_[index];
            if (node.type == NodeType::ImageFilter) {
                outset += node.imageFilter.samplingOutset();
            } else if (node.type == NodeType::Offset) {
                outset += std::max(
                    std::abs(node.offsetX),
                    std::abs(node.offsetY));
            }
        }
        return outset;
    }

    float outputOutset() const
    {
        float outset = 0.0f;
        for (std::size_t index = 0; index < nodes_.size(); ++index) {
            const Node &node = nodes_[index];
            if (node.type == NodeType::ImageFilter) {
                outset += node.imageFilter.outputOutset();
            } else if (node.type == NodeType::Offset) {
                outset += std::max(
                    std::abs(node.offsetX),
                    std::abs(node.offsetY));
            }
        }
        return outset;
    }

private:
    std::vector<Node> nodes_;
};

/// Optional effects attached to a saveLayer operation.
///
/// `imageFilter` processes the layer's own content. `backdropFilter` processes
/// the pixels already drawn behind the layer before the layer content is added.
/// Filters run in the order stored by ImageFilterChain and are evaluated only
/// when the corresponding saveLayer is restored.
class WSC_API LayerOptions
{
public:
    /// Replace the layer-content filter/chain. Later calls replace earlier ones.
    LayerOptions &setImageFilter(const ImageFilter &filter)
    {
        imageFilter_ = filter;
        imageFilterChain_ = ImageFilterChain(filter);
        return *this;
    }

    LayerOptions &setImageFilter(const ImageFilterChain &filters)
    {
        imageFilter_ = filters.empty()
            || filters[0].type != ImageFilterChain::NodeType::ImageFilter
            ? ImageFilter() : filters[0].imageFilter;
        imageFilterChain_ = filters;
        return *this;
    }

    /// Replace the backdrop filter/chain. A backdrop observes only pixels
    /// recorded before the matching saveLayer().
    LayerOptions &setBackdropFilter(const ImageFilter &filter)
    {
        backdropFilter_ = filter;
        backdropFilterChain_ = ImageFilterChain(filter);
        return *this;
    }

    LayerOptions &setBackdropFilter(const ImageFilterChain &filters)
    {
        backdropFilter_ = filters.empty()
            || filters[0].type != ImageFilterChain::NodeType::ImageFilter
            ? ImageFilter() : filters[0].imageFilter;
        backdropFilterChain_ = filters;
        return *this;
    }

    const ImageFilter &imageFilter() const { return imageFilter_; }

    const ImageFilter &backdropFilter() const { return backdropFilter_; }

    const ImageFilterChain &imageFilterChain() const
    {
        return imageFilterChain_;
    }

    const ImageFilterChain &backdropFilterChain() const
    {
        return backdropFilterChain_;
    }

    bool hasImageFilter() const { return imageFilterChain_.isValid(); }

    bool hasBackdropFilter() const
    {
        return backdropFilterChain_.isValid();
    }

private:
    ImageFilter imageFilter_;
    ImageFilter backdropFilter_;
    ImageFilterChain imageFilterChain_;
    ImageFilterChain backdropFilterChain_;
};

} // namespace wsc
