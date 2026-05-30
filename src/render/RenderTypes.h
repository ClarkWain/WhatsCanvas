#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <glm.hpp>

class RenderContext;
struct ScissorState;

struct ImageResourceHandle {
    std::uint32_t value = 0;

    bool isValid() const { return value != 0; }
};

class ImageResource
{
public:
    virtual ~ImageResource() = default;

    virtual bool isValid() const = 0;
    virtual void bind(const RenderContext &context) const = 0;
};

using SharedImageResource = std::shared_ptr<ImageResource>;

using TextureHandle = ImageResourceHandle;

class ClipMaskResource
{
public:
    virtual ~ClipMaskResource() = default;

    virtual bool isValid() const = 0;
    virtual void apply(const RenderContext &context, const ScissorState &scissor, std::size_t clipIndex) const = 0;
};

using SharedClipMaskResource = std::shared_ptr<ClipMaskResource>;

struct ScissorState {
    bool enabled = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ClipMaskPath {
    std::vector<float> points;
    glm::mat4 transform = glm::mat4(1.0f);
};

struct ClipMaskState {
    std::vector<SharedClipMaskResource> resources;
    std::uint64_t fingerprint = 0;

    bool hasPaths() const { return !resources.empty(); }
    std::size_t pathCount() const { return resources.size(); }
};

enum class DrawBlendMode {
    SrcOver,
    Src,
    Dst,
    Clear,
    SrcIn,
    DstIn,
    SrcOut,
    DstOut,
    SrcAtop,
    DstAtop,
    Xor,
    Add,
    Multiply,
    Screen
};

enum class DrawImageSampling {
    Linear,
    Nearest,
    MipmapLinear
};

enum class DrawImageTileMode {
    Clamp,
    Repeat,
    Mirror,
    Decal
};