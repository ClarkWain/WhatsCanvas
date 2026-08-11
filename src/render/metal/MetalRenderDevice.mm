// Metal render backend for WhatsCanvas.
//
// Compilation model: on Apple platforms with WHATSCANVAS_ENABLE_METAL, this
// file is compiled as Objective-C++ (`.mm`) with ARC enabled by the CMake
// per-source flag. On non-Apple hosts, the CMake glue compiles the sibling
// `MetalRenderDevice.cpp` inert stub instead. Inside this translation unit we
// still route every public entry point through `#if defined(...)` so the class
// remains referenceable when the option is toggled off.

#include "MetalRenderDevice.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "../CommandDrawListEncoder.h"
#include "../IRenderTarget.h"
#include "../IRenderer.h"
#include "../RenderTypes.h"
#include "../Surface.h"
#include "command/DrawCommand.h"
#include "core/LogInternal.h"

#if defined(WHATSCANVAS_ENABLE_METAL) && defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

namespace {

// -----------------------------------------------------------------------------
// Metal Shading Language sources.
//
// All shaders are compiled from source at initializeBackend() via
// `newLibraryWithSource:options:error:`. This keeps the build system free of a
// `xcrun metal`/metallib toolchain requirement and matches the runtime-compile
// pattern used for GLSL in the OpenGL backend.
//
// Coordinate convention: the `CommandDrawListEncoder` emits vertex positions in
// GL-style NDC (Y up). Metal framebuffers have their origin at the top-left, so
// each vertex shader flips Y before returning the clip-space position. The
// resulting readPixelsRGBA output then matches the OpenGL / Vulkan backends
// row-for-row.
//
// All pipelines share a common CBuffer layout for scissor bookkeeping (handled
// on the CPU side via setScissorRect:) and per-primitive uniforms passed via
// setVertex/FragmentBytes.
constexpr const char *kMetalShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

// ---- Solid pipeline: interleaved position(x,y) + rgba8 color + coverage ----

struct SolidVSInput {
    float2 position [[attribute(0)]];
    uchar4 color    [[attribute(1)]];
    uchar  coverage [[attribute(2)]];
};

struct SolidVSOut {
    float4 position [[position]];
    float4 color;
    float  coverage;
};

vertex SolidVSOut solid_vs(SolidVSInput in [[stage_in]])
{
    SolidVSOut out;
    // Flip Y: encoder produces GL-style NDC (Y up), Metal wants Y down.
    out.position = float4(in.position.x, -in.position.y, 0.0, 1.0);
    out.color = float4(in.color) / 255.0;
    out.coverage = float(in.coverage) / 255.0;
    return out;
}

fragment float4 solid_fs(SolidVSOut in [[stage_in]])
{
    float4 c = in.color;
    c.a *= in.coverage;
    return c;
}

// ---- Textured pipeline: position(x,y) + uv(x,y) + per-vertex tint(rgba8),
// sampled with a global tint/alpha uniform ----

struct TexturedVSInput {
    float2 position [[attribute(0)]];
    float2 uv       [[attribute(1)]];
    uchar4 vertexTint [[attribute(2)]];
};

struct TexturedVSOut {
    float4 position [[position]];
    float2 uv;
    float4 vertexTint;
};

struct TexturedUniforms {
    float4 tint;      // rgba multiplier
    float4 params;    // x = layerAlpha, y = unused, z = unused, w = unused
};

vertex TexturedVSOut textured_vs(TexturedVSInput in [[stage_in]])
{
    TexturedVSOut out;
    out.position = float4(in.position.x, -in.position.y, 0.0, 1.0);
    out.uv = in.uv;
    out.vertexTint = float4(in.vertexTint) / 255.0;
    return out;
}

fragment float4 textured_fs(TexturedVSOut in [[stage_in]],
                            texture2d<float> tex [[texture(0)]],
                            sampler samp [[sampler(0)]],
                            constant TexturedUniforms &u [[buffer(0)]])
{
    float4 s = tex.sample(samp, in.uv);
    float4 c = s * u.tint * in.vertexTint;
    c.a *= u.params.x;
    return c;
}

// Alpha-only path (glyph atlas / mask sampled as R8): fragment reads the red
// channel and multiplies it by the tint color to produce coloured coverage.
fragment float4 textured_alpha_fs(TexturedVSOut in [[stage_in]],
                                  texture2d<float> tex [[texture(0)]],
                                  sampler samp [[sampler(0)]],
                                  constant TexturedUniforms &u [[buffer(0)]])
{
    float coverage = tex.sample(samp, in.uv).r;
    float4 c = u.tint * in.vertexTint;
    c.a *= coverage * u.params.x;
    return c;
}

// ---- Gradient pipeline: fragment evaluates a linear/radial gradient ----

struct GradientVSInput {
    float2 position      [[attribute(0)]];
    float2 localPosition [[attribute(1)]];
};

struct GradientVSOut {
    float4 position [[position]];
    float2 localPosition;
};

struct GradientUniforms {
    // params.x = gradient type (1 linear, 2 radial)
    // params.y = tile mode (0 clamp, 1 repeat, 2 mirror, 3 decal)
    // params.z = stop count (as float)
    // params.w = radial radius (or unused for linear)
    float4 params;
    float4 linearStart;  // xy used
    float4 linearEnd;    // xy used
    float4 radialCenter; // xy used
    float4 stops[8];     // r,g,b,a per stop
    float4 offsets;      // packed 4 offsets
    float4 offsetsHigh;  // packed offsets 4..7
};

vertex GradientVSOut gradient_vs(GradientVSInput in [[stage_in]])
{
    GradientVSOut out;
    out.position = float4(in.position.x, -in.position.y, 0.0, 1.0);
    out.localPosition = in.localPosition;
    return out;
}

static float applyTile(float t, int mode)
{
    if (mode == 0) {
        return saturate(t);
    } else if (mode == 1) {
        // repeat
        return t - floor(t);
    } else if (mode == 2) {
        // mirror
        float f = fmod(fabs(t), 2.0);
        return f > 1.0 ? 2.0 - f : f;
    }
    // decal: caller responsible for clipping alpha
    return t;
}

fragment float4 gradient_fs(GradientVSOut in [[stage_in]],
                            constant GradientUniforms &u [[buffer(0)]])
{
    int type = int(u.params.x);
    int tileMode = int(u.params.y);
    int stopCount = int(u.params.z);

    float t = 0.0;
    if (type == 1) {
        float2 s = u.linearStart.xy;
        float2 e = u.linearEnd.xy;
        float2 d = e - s;
        float denom = max(dot(d, d), 1e-8);
        t = dot(in.localPosition - s, d) / denom;
    } else if (type == 2) {
        float2 c = u.radialCenter.xy;
        float r = max(u.params.w, 1e-8);
        t = length(in.localPosition - c) / r;
    }

    float alpha = 1.0;
    if (tileMode == 3 && (t < 0.0 || t > 1.0)) {
        alpha = 0.0;
    }
    float tt = applyTile(t, tileMode);

    // Unpack 8 stop offsets from two float4s (up to 8 stops).
    float offsets[8];
    offsets[0] = u.offsets.x;     offsets[1] = u.offsets.y;
    offsets[2] = u.offsets.z;     offsets[3] = u.offsets.w;
    offsets[4] = u.offsetsHigh.x; offsets[5] = u.offsetsHigh.y;
    offsets[6] = u.offsetsHigh.z; offsets[7] = u.offsetsHigh.w;

    if (stopCount <= 0) {
        return float4(0.0);
    }
    if (stopCount == 1) {
        float4 c = u.stops[0];
        c.a *= alpha;
        return c;
    }

    // Piecewise-linear evaluation across the stop array.
    if (tt <= offsets[0]) {
        float4 c = u.stops[0];
        c.a *= alpha;
        return c;
    }
    for (int i = 1; i < 8; ++i) {
        if (i >= stopCount) break;
        if (tt <= offsets[i]) {
            float span = max(offsets[i] - offsets[i - 1], 1e-8);
            float f = (tt - offsets[i - 1]) / span;
            float4 c = mix(u.stops[i - 1], u.stops[i], f);
            c.a *= alpha;
            return c;
        }
    }
    float4 c = u.stops[stopCount - 1];
    c.a *= alpha;
    return c;
}

// ---- Clip fill pipeline: full-target solid color modulated by a coverage
// texture's red channel ----

struct ClipVSInput {
    float2 position [[attribute(0)]];
};

struct ClipVSOut {
    float4 position [[position]];
    float2 uv;
};

struct ClipUniforms {
    float4 color;
    float4 uvScaleOffset; // xy = scale, zw = offset
};

vertex ClipVSOut clip_vs(ClipVSInput in [[stage_in]])
{
    ClipVSOut out;
    out.position = float4(in.position.x, -in.position.y, 0.0, 1.0);
    // Full-target quad in Y-down NDC (-1..1). After the vertex Y-flip above
    // the vertex at NDC (-1, -1) ends up at framebuffer top-left, so we want
    // UV (0, 0) there too — Metal textures have top-left origin like the
    // framebuffer. No extra Y-flip on the UV: just map NDC linearly to 0..1.
    // The Canvas layer only ever uses the identity uv-scale/offset for
    // clip fills, so the transform is baked into the shader instead of a
    // per-draw uniform (avoids a vertex-buffer(0) collision with stage_in).
    out.uv = in.position * 0.5 + 0.5;
    return out;
}

fragment float4 clip_fs(ClipVSOut in [[stage_in]],
                        texture2d<float> mask [[texture(0)]],
                        sampler samp [[sampler(0)]],
                        constant ClipUniforms &u [[buffer(0)]])
{
    float coverage = mask.sample(samp, in.uv).r;
    float4 c = u.color;
    c.a *= coverage;
    return c;
}

// ---- Gaussian blur pipeline (separable) ----
// A full-target quad drives a fragment shader that walks a 1D kernel along
// `direction`. filterImageResource() runs two passes: first horizontal into
// a scratch target, then vertical into the final target.

struct BlurVSInput {
    float2 position [[attribute(0)]];
    float2 uv       [[attribute(1)]];
};

struct BlurVSOut {
    float4 position [[position]];
    float2 uv;
};

constant int kMetalBlurMaxTaps = 31;

struct BlurUniforms {
    float2 direction;      // per-pixel step (1/W, 0) or (0, 1/H)
    float2 padding;
    int    tapCount;       // number of one-sided samples (excluding center)
    int    tileMode;       // 0 = clamp, 1 = decal
    float  padding2[2];
    float4 weights[32];    // weights[i].x used; kept as float4 for alignment
};

vertex BlurVSOut blur_vs(BlurVSInput in [[stage_in]])
{
    BlurVSOut out;
    out.position = float4(in.position.x, -in.position.y, 0.0, 1.0);
    out.uv = in.uv;
    return out;
}

fragment float4 blur_fs(BlurVSOut in [[stage_in]],
                        texture2d<float> src [[texture(0)]],
                        sampler samp [[sampler(0)]],
                        constant BlurUniforms &u [[buffer(0)]])
{
    float4 acc = src.sample(samp, in.uv) * u.weights[0].x;
    for (int i = 1; i <= kMetalBlurMaxTaps; ++i) {
        if (i > u.tapCount) break;
        float2 off = u.direction * float(i);
        float2 uvA = in.uv + off;
        float2 uvB = in.uv - off;
        float4 sA = src.sample(samp, uvA);
        float4 sB = src.sample(samp, uvB);
        if (u.tileMode == 1) {
            if (uvA.x < 0.0 || uvA.x > 1.0 || uvA.y < 0.0 || uvA.y > 1.0) sA = float4(0.0);
            if (uvB.x < 0.0 || uvB.x > 1.0 || uvB.y < 0.0 || uvB.y > 1.0) sB = float4(0.0);
        }
        acc += (sA + sB) * u.weights[i].x;
    }
    return acc;
}
)MSL";

// -----------------------------------------------------------------------------
// Blend descriptor helper. Mirrors the subset of blend modes emitted by the
// CommandDrawListEncoder into DrawPrimitive::blendMode (matches
// VulkanRenderDevice::SolidBlendMode ordering).
enum class MetalBlendMode
{
    SrcOver = 0,
    Src = 1,
    Add = 2,
    Multiply = 3,
    Screen = 4,
};

MetalBlendMode blendModeFromInt(int value)
{
    switch (value) {
    case 1: return MetalBlendMode::Src;
    case 2: return MetalBlendMode::Add;
    case 3: return MetalBlendMode::Multiply;
    case 4: return MetalBlendMode::Screen;
    case 0:
    default:
        return MetalBlendMode::SrcOver;
    }
}

void configureBlend(MTLRenderPipelineColorAttachmentDescriptor *color, MetalBlendMode mode)
{
    color.blendingEnabled = YES;
    color.writeMask = MTLColorWriteMaskAll;
    switch (mode) {
    case MetalBlendMode::Src:
        color.blendingEnabled = NO;
        break;
    case MetalBlendMode::Add:
        color.rgbBlendOperation = MTLBlendOperationAdd;
        color.alphaBlendOperation = MTLBlendOperationAdd;
        color.sourceRGBBlendFactor = MTLBlendFactorOne;
        color.sourceAlphaBlendFactor = MTLBlendFactorOne;
        color.destinationRGBBlendFactor = MTLBlendFactorOne;
        color.destinationAlphaBlendFactor = MTLBlendFactorOne;
        break;
    case MetalBlendMode::Multiply:
        color.rgbBlendOperation = MTLBlendOperationAdd;
        color.alphaBlendOperation = MTLBlendOperationAdd;
        color.sourceRGBBlendFactor = MTLBlendFactorDestinationColor;
        color.sourceAlphaBlendFactor = MTLBlendFactorDestinationAlpha;
        color.destinationRGBBlendFactor = MTLBlendFactorZero;
        color.destinationAlphaBlendFactor = MTLBlendFactorZero;
        break;
    case MetalBlendMode::Screen:
        color.rgbBlendOperation = MTLBlendOperationAdd;
        color.alphaBlendOperation = MTLBlendOperationAdd;
        color.sourceRGBBlendFactor = MTLBlendFactorOne;
        color.sourceAlphaBlendFactor = MTLBlendFactorOne;
        color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceColor;
        color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case MetalBlendMode::SrcOver:
    default:
        // Standard premultiplied alpha over.
        color.rgbBlendOperation = MTLBlendOperationAdd;
        color.alphaBlendOperation = MTLBlendOperationAdd;
        color.sourceRGBBlendFactor = MTLBlendFactorOne;
        color.sourceAlphaBlendFactor = MTLBlendFactorOne;
        color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    }
}

// Convert DrawList blend mode integer (produced by encoder) to Metal blend
// factors on a MTLRenderPipelineColorAttachmentDescriptor.
void configureBlendForDrawList(MTLRenderPipelineColorAttachmentDescriptor *color, int blendMode)
{
    configureBlend(color, blendModeFromInt(blendMode));
}

// Sampler descriptor helper.
id<MTLSamplerState> makeSampler(id<MTLDevice> device, int sampling, int tileMode)
{
    MTLSamplerDescriptor *d = [[MTLSamplerDescriptor alloc] init];
    switch (sampling) {
    case 1: // nearest
        d.minFilter = MTLSamplerMinMagFilterNearest;
        d.magFilter = MTLSamplerMinMagFilterNearest;
        d.mipFilter = MTLSamplerMipFilterNearest;
        break;
    case 2: // mipmap-linear
        d.minFilter = MTLSamplerMinMagFilterLinear;
        d.magFilter = MTLSamplerMinMagFilterLinear;
        d.mipFilter = MTLSamplerMipFilterLinear;
        break;
    case 0:
    default:
        d.minFilter = MTLSamplerMinMagFilterLinear;
        d.magFilter = MTLSamplerMinMagFilterLinear;
        d.mipFilter = MTLSamplerMipFilterNotMipmapped;
        break;
    }
    MTLSamplerAddressMode addr = MTLSamplerAddressModeClampToEdge;
    switch (tileMode) {
    case 1: addr = MTLSamplerAddressModeRepeat; break;
    case 2: addr = MTLSamplerAddressModeMirrorRepeat; break;
    case 3: addr = MTLSamplerAddressModeClampToZero; break;
    case 0:
    default: addr = MTLSamplerAddressModeClampToEdge; break;
    }
    d.sAddressMode = addr;
    d.tAddressMode = addr;
    d.rAddressMode = addr;
    return [device newSamplerStateWithDescriptor:d];
}

MTLStorageMode preferredTextureStorageMode(id<MTLDevice> device)
{
    // On unified-memory devices (Apple Silicon, all iOS) shared storage lets
    // both CPU and GPU access the resource without an explicit blit. On Intel
    // Macs we use managed and rely on didModifyRange/synchronizeResource.
    (void)device;
#if TARGET_OS_IPHONE || TARGET_OS_TV
    return MTLStorageModeShared;
#else
    if (@available(macOS 10.15, *)) {
        if ([device hasUnifiedMemory]) {
            return MTLStorageModeShared;
        }
    }
    return MTLStorageModeManaged;
#endif
}

MTLStorageMode preferredBufferStorageMode(id<MTLDevice> device)
{
    (void)device;
#if TARGET_OS_IPHONE || TARGET_OS_TV
    return MTLStorageModeShared;
#else
    if (@available(macOS 10.15, *)) {
        if ([device hasUnifiedMemory]) {
            return MTLStorageModeShared;
        }
    }
    return MTLStorageModeManaged;
#endif
}

// -----------------------------------------------------------------------------
// Owned image resource.
class MetalTextureResource final : public ImageResource
{
public:
    MetalTextureResource(id<MTLTexture> texture, int w, int h, bool isAlpha, bool owned)
        : texture_(texture), width_(w), height_(h), isAlpha_(isAlpha), owned_(owned) {}

    ~MetalTextureResource() override
    {
        // ARC releases id<MTLTexture> automatically when the strong reference
        // drops. Wrapped external textures share ownership; that copy is only
        // released by whoever created the source retain.
        texture_ = nil;
    }

    bool isValid() const override { return texture_ != nil && width_ > 0 && height_ > 0; }
    ImageOrigin origin() const override { return ImageOrigin::TopLeft; }
    ImageAlphaType alphaType() const override { return ImageAlphaType::Straight; }
    bool isAlphaOnly() const override { return isAlpha_; }
    ImageResourceHandle nativeHandle() const override
    {
        return {static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>((__bridge void *)texture_))};
    }
    void bind(const RenderContext & /*context*/) const override
    {
        // Metal has no notion of "bind an image resource globally": binding is
        // per-encoder. Rendering paths retrieve the texture directly via
        // metalTexture().
    }

    bool updateRGBA(int x, int y, int width, int height, const unsigned char *pixels,
                    bool /*regenerateMipmaps*/) override
    {
        if (texture_ == nil || pixels == nullptr || width <= 0 || height <= 0) {
            return false;
        }
        if (x < 0 || y < 0 || x + width > width_ || y + height > height_) {
            return false;
        }
        MTLRegion region = MTLRegionMake2D(x, y, width, height);
        NSUInteger bytesPerRow = static_cast<NSUInteger>(width) * (isAlpha_ ? 1u : 4u);
        [texture_ replaceRegion:region
                    mipmapLevel:0
                      withBytes:pixels
                    bytesPerRow:bytesPerRow];
        return true;
    }

    id<MTLTexture> metalTexture() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool owned() const { return owned_; }

private:
    id<MTLTexture> texture_ = nil;
    int width_ = 0;
    int height_ = 0;
    bool isAlpha_ = false;
    bool owned_ = false;
};

// -----------------------------------------------------------------------------
// Owned clip mask resource. The mask is baked into a single-channel texture at
// creation time; sampling happens in the ClipFill fragment shader.
class MetalClipMaskResource final : public ClipMaskResource
{
public:
    MetalClipMaskResource(std::vector<float> points, std::vector<float> coverage,
                          const glm::mat4 &transform)
        : points_(std::move(points)), coverage_(std::move(coverage)), transform_(transform) {}

    bool isValid() const override { return points_.size() >= 6; }
    void apply(const RenderContext & /*context*/, const ScissorState & /*scissor*/,
               std::size_t /*clipIndex*/) const override
    {
        // Metal's clip mask is sampled during draw; nothing to bind up-front.
    }

    const std::vector<float> &points() const { return points_; }
    const std::vector<float> &coverage() const { return coverage_; }
    const glm::mat4 &transform() const { return transform_; }

private:
    std::vector<float> points_;
    std::vector<float> coverage_;
    glm::mat4 transform_{1.0f};
};

// -----------------------------------------------------------------------------
// Owned render target.
class MetalRenderTarget final : public IRenderTarget
{
public:
    MetalRenderTarget(id<MTLTexture> texture, SharedImageResource resource, int width, int height)
        : texture_(texture), imageResource_(std::move(resource)), width_(width), height_(height) {}

    ~MetalRenderTarget() override { texture_ = nil; }

    bool isValid() const override { return texture_ != nil && width_ > 0 && height_ > 0; }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool begin(const OffscreenRenderRequest &request) override
    {
        pendingRequest_ = request;
        hasPending_ = true;
        activated_ = false;
        return true;
    }
    void activate() override
    {
        // Metal render passes are described per-draw via a
        // MTLRenderPassDescriptor built at executeDrawList() time, so the
        // "activation" step is a lightweight state flip.
        activated_ = true;
    }
    bool isActivated() const override { return activated_; }
    void end() override { activated_ = false; hasPending_ = false; }
    SharedImageResource getImageResource() const override { return imageResource_; }

    id<MTLTexture> metalTexture() const { return texture_; }
    bool hasPending() const { return hasPending_; }
    const OffscreenRenderRequest *pendingRequest() const { return hasPending_ ? &pendingRequest_ : nullptr; }

private:
    id<MTLTexture> texture_ = nil;
    SharedImageResource imageResource_;
    int width_ = 0;
    int height_ = 0;
    bool activated_ = false;
    bool hasPending_ = false;
    OffscreenRenderRequest pendingRequest_{};
};

// -----------------------------------------------------------------------------
// Pipeline cache key.
enum class MetalPipelineKind
{
    Solid = 0,
    Textured = 1,
    TexturedAlpha = 2,
    Gradient = 3,
    ClipFill = 4,
    Blur = 5,
};

struct MetalPipelineKey
{
    MetalPipelineKind kind;
    int blendMode = 0;

    bool operator==(const MetalPipelineKey &o) const
    {
        return kind == o.kind && blendMode == o.blendMode;
    }
};

struct MetalPipelineKeyHash
{
    std::size_t operator()(const MetalPipelineKey &k) const noexcept
    {
        return (static_cast<std::size_t>(k.kind) << 8) ^ static_cast<std::size_t>(k.blendMode);
    }
};

struct MetalSamplerKey
{
    int sampling = 0;
    int tileMode = 0;

    bool operator==(const MetalSamplerKey &o) const
    {
        return sampling == o.sampling && tileMode == o.tileMode;
    }
};

struct MetalSamplerKeyHash
{
    std::size_t operator()(const MetalSamplerKey &k) const noexcept
    {
        return (static_cast<std::size_t>(k.sampling) << 4) ^ static_cast<std::size_t>(k.tileMode);
    }
};

} // namespace

// -----------------------------------------------------------------------------
// Backend context (private state; opaque forward-declared in the header).
struct MetalRenderDevice::MetalContext
{
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    id<MTLLibrary> library = nil;

    // Vertex descriptors keyed by pipeline kind.
    MTLVertexDescriptor *solidVertexDesc = nil;
    MTLVertexDescriptor *texturedVertexDesc = nil;
    MTLVertexDescriptor *gradientVertexDesc = nil;
    MTLVertexDescriptor *clipVertexDesc = nil;
    MTLVertexDescriptor *blurVertexDesc = nil;

    // Cached pipeline states.
    std::unordered_map<MetalPipelineKey, id<MTLRenderPipelineState>, MetalPipelineKeyHash> pipelines;
    std::unordered_map<MetalSamplerKey, id<MTLSamplerState>, MetalSamplerKeyHash> samplers;

    // Reusable identity sampler for clip masks (linear + clamp).
    id<MTLSamplerState> clipSampler = nil;

    // Most recent render-target texture rendered into. `readPixelsRGBA`
    // consumes this because the IRenderDevice contract does not carry the
    // target through the readback call.
    id<MTLTexture> lastReadbackTexture = nil;
    int lastReadbackWidth = 0;
    int lastReadbackHeight = 0;

    // Stats.
    std::size_t imageTextureCount = 0;
    std::size_t renderTargetCount = 0;

    bool deviceReady = false;
    std::string deviceName;
};

// -----------------------------------------------------------------------------
// Availability.
bool MetalRenderDevice::isAvailable() { return true; }

MetalRenderDevice::MetalRenderDevice()
    : context_(std::make_unique<MetalContext>())
{
}

MetalRenderDevice::~MetalRenderDevice()
{
    if (backendInitialized_) {
        finalizeBackend();
    }
}

// -----------------------------------------------------------------------------
// Lifecycle.
void MetalRenderDevice::initializeBackend()
{
    if (context_->deviceReady) {
        return;
    }
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            WSC_LOG_ERROR("MetalRenderDevice", "No Metal device available on this system.");
            return;
        }
        context_->device = device;
        context_->commandQueue = [device newCommandQueue];
        if (context_->commandQueue == nil) {
            WSC_LOG_ERROR("MetalRenderDevice", "Failed to allocate Metal command queue.");
            context_->device = nil;
            return;
        }

        NSError *error = nil;
        MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
#if defined(__MAC_10_15) || defined(__IPHONE_13_0)
        opts.languageVersion = MTLLanguageVersion2_2;
#endif
        NSString *src = [NSString stringWithUTF8String:kMetalShaderSource];
        id<MTLLibrary> library = [device newLibraryWithSource:src options:opts error:&error];
        if (library == nil) {
            const char *msg = error ? [[error localizedDescription] UTF8String] : "<unknown>";
            WSC_LOG_ERROR("MetalRenderDevice", (std::string("Failed to compile MSL library: ") + msg).c_str());
            context_->commandQueue = nil;
            context_->device = nil;
            return;
        }
        context_->library = library;

        // Solid vertex descriptor: pos float2 (offset 0), color uchar4 (offset 8),
        // coverage uchar (offset 12). Stride 16 (CompactSolidVertex).
        MTLVertexDescriptor *sd = [MTLVertexDescriptor vertexDescriptor];
        sd.attributes[0].format = MTLVertexFormatFloat2;
        sd.attributes[0].offset = 0;
        sd.attributes[0].bufferIndex = 0;
        sd.attributes[1].format = MTLVertexFormatUChar4;
        sd.attributes[1].offset = 8;
        sd.attributes[1].bufferIndex = 0;
        sd.attributes[2].format = MTLVertexFormatUChar;
        sd.attributes[2].offset = 12;
        sd.attributes[2].bufferIndex = 0;
        sd.layouts[0].stride = 16;
        sd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        context_->solidVertexDesc = sd;

        // Textured vertex descriptor: pos float2, uv float2, tint uchar4, stride 20.
        MTLVertexDescriptor *td = [MTLVertexDescriptor vertexDescriptor];
        td.attributes[0].format = MTLVertexFormatFloat2;
        td.attributes[0].offset = 0;
        td.attributes[0].bufferIndex = 0;
        td.attributes[1].format = MTLVertexFormatFloat2;
        td.attributes[1].offset = 8;
        td.attributes[1].bufferIndex = 0;
        td.attributes[2].format = MTLVertexFormatUChar4;
        td.attributes[2].offset = 16;
        td.attributes[2].bufferIndex = 0;
        td.layouts[0].stride = 20;
        td.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        context_->texturedVertexDesc = td;

        // Gradient vertex descriptor: pos float2, localPos float2, stride 16.
        MTLVertexDescriptor *gd = [MTLVertexDescriptor vertexDescriptor];
        gd.attributes[0].format = MTLVertexFormatFloat2;
        gd.attributes[0].offset = 0;
        gd.attributes[0].bufferIndex = 0;
        gd.attributes[1].format = MTLVertexFormatFloat2;
        gd.attributes[1].offset = 8;
        gd.attributes[1].bufferIndex = 0;
        gd.layouts[0].stride = 16;
        gd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        context_->gradientVertexDesc = gd;

        // Clip vertex descriptor: pos float2, stride 8.
        MTLVertexDescriptor *cd = [MTLVertexDescriptor vertexDescriptor];
        cd.attributes[0].format = MTLVertexFormatFloat2;
        cd.attributes[0].offset = 0;
        cd.attributes[0].bufferIndex = 0;
        cd.layouts[0].stride = 8;
        cd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        context_->clipVertexDesc = cd;

        // Blur vertex descriptor: pos float2, uv float2, stride 16.
        MTLVertexDescriptor *bd = [MTLVertexDescriptor vertexDescriptor];
        bd.attributes[0].format = MTLVertexFormatFloat2;
        bd.attributes[0].offset = 0;
        bd.attributes[0].bufferIndex = 0;
        bd.attributes[1].format = MTLVertexFormatFloat2;
        bd.attributes[1].offset = 8;
        bd.attributes[1].bufferIndex = 0;
        bd.layouts[0].stride = 16;
        bd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        context_->blurVertexDesc = bd;

        // Clip mask sampler: linear + clamp-to-edge.
        MTLSamplerDescriptor *sampDesc = [[MTLSamplerDescriptor alloc] init];
        sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
        sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
        sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sampDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        context_->clipSampler = [device newSamplerStateWithDescriptor:sampDesc];

        NSString *name = [device name];
        context_->deviceName = name ? [name UTF8String] : "Metal Device";
        context_->deviceReady = true;
    }
    backendInitialized_ = true;
    WSC_LOG_INFO("MetalRenderDevice",
                 (std::string("Metal backend initialized on device: ") + context_->deviceName).c_str());
}

void MetalRenderDevice::finalizeBackend()
{
    if (!backendInitialized_) {
        return;
    }
    context_->pipelines.clear();
    context_->samplers.clear();
    context_->clipSampler = nil;
    context_->solidVertexDesc = nil;
    context_->texturedVertexDesc = nil;
    context_->gradientVertexDesc = nil;
    context_->blurVertexDesc = nil;
    context_->clipVertexDesc = nil;
    context_->library = nil;
    context_->commandQueue = nil;
    context_->device = nil;
    context_->deviceReady = false;
    backendInitialized_ = false;
}

bool MetalRenderDevice::isDeviceReady() const
{
    return context_ && context_->deviceReady;
}

const std::string &MetalRenderDevice::selectedDeviceName() const
{
    static const std::string kEmpty;
    return context_ ? context_->deviceName : kEmpty;
}

std::uintptr_t MetalRenderDevice::nativeHandle(int which) const
{
    if (!context_ || !context_->deviceReady) {
        return 0;
    }
    switch (which) {
    case 0: return reinterpret_cast<std::uintptr_t>((__bridge void *)context_->device);
    case 1: return reinterpret_cast<std::uintptr_t>((__bridge void *)context_->commandQueue);
    case 2: return reinterpret_cast<std::uintptr_t>((__bridge void *)context_->lastReadbackTexture);
    default: return 0;
    }
}

// -----------------------------------------------------------------------------
// Pipeline / sampler helpers.
namespace {

id<MTLRenderPipelineState> obtainPipeline(MetalRenderDevice::MetalContext &ctx,
                                          MetalPipelineKind kind, int blendMode)
{
    MetalPipelineKey key{kind, blendMode};
    auto it = ctx.pipelines.find(key);
    if (it != ctx.pipelines.end()) {
        return it->second;
    }
    NSString *vsName = nil;
    NSString *fsName = nil;
    MTLVertexDescriptor *vd = nil;
    switch (kind) {
    case MetalPipelineKind::Solid:
        vsName = @"solid_vs";
        fsName = @"solid_fs";
        vd = ctx.solidVertexDesc;
        break;
    case MetalPipelineKind::Textured:
        vsName = @"textured_vs";
        fsName = @"textured_fs";
        vd = ctx.texturedVertexDesc;
        break;
    case MetalPipelineKind::TexturedAlpha:
        vsName = @"textured_vs";
        fsName = @"textured_alpha_fs";
        vd = ctx.texturedVertexDesc;
        break;
    case MetalPipelineKind::Gradient:
        vsName = @"gradient_vs";
        fsName = @"gradient_fs";
        vd = ctx.gradientVertexDesc;
        break;
    case MetalPipelineKind::ClipFill:
        vsName = @"clip_vs";
        fsName = @"clip_fs";
        vd = ctx.clipVertexDesc;
        break;
    case MetalPipelineKind::Blur:
        vsName = @"blur_vs";
        fsName = @"blur_fs";
        vd = ctx.blurVertexDesc;
        break;
    }
    id<MTLFunction> vs = [ctx.library newFunctionWithName:vsName];
    id<MTLFunction> fs = [ctx.library newFunctionWithName:fsName];
    if (vs == nil || fs == nil) {
        return nil;
    }
    MTLRenderPipelineDescriptor *desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vs;
    desc.fragmentFunction = fs;
    desc.vertexDescriptor = vd;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    configureBlendForDrawList(desc.colorAttachments[0], blendMode);
    NSError *err = nil;
    id<MTLRenderPipelineState> pso = [ctx.device newRenderPipelineStateWithDescriptor:desc error:&err];
    if (pso == nil) {
        const char *msg = err ? [[err localizedDescription] UTF8String] : "<unknown>";
        WSC_LOG_ERROR("MetalRenderDevice",
                      (std::string("Failed to create render pipeline: ") + msg).c_str());
        return nil;
    }
    ctx.pipelines.emplace(key, pso);
    return pso;
}

id<MTLSamplerState> obtainSampler(MetalRenderDevice::MetalContext &ctx, int sampling, int tileMode)
{
    MetalSamplerKey key{sampling, tileMode};
    auto it = ctx.samplers.find(key);
    if (it != ctx.samplers.end()) {
        return it->second;
    }
    id<MTLSamplerState> s = makeSampler(ctx.device, sampling, tileMode);
    ctx.samplers.emplace(key, s);
    return s;
}

MTLViewport makeViewport(int width, int height)
{
    MTLViewport vp;
    vp.originX = 0.0;
    vp.originY = 0.0;
    vp.width = static_cast<double>(width);
    vp.height = static_cast<double>(height);
    vp.znear = 0.0;
    vp.zfar = 1.0;
    return vp;
}

void applyScissor(id<MTLRenderCommandEncoder> encoder, const wsc::DrawPrimitive &prim,
                  int targetWidth, int targetHeight)
{
    MTLScissorRect rect;
    if (prim.scissorEnabled) {
        int x = std::max(prim.scissorX, 0);
        int y = std::max(prim.scissorY, 0);
        int w = std::max(prim.scissorWidth, 0);
        int h = std::max(prim.scissorHeight, 0);
        // Clamp so we never overflow the framebuffer (Metal validation strict).
        if (x >= targetWidth || y >= targetHeight) {
            x = 0; y = 0; w = 0; h = 0;
        }
        if (x + w > targetWidth) w = targetWidth - x;
        if (y + h > targetHeight) h = targetHeight - y;
        rect.x = static_cast<NSUInteger>(x);
        rect.y = static_cast<NSUInteger>(y);
        rect.width = static_cast<NSUInteger>(w);
        rect.height = static_cast<NSUInteger>(h);
    } else {
        rect.x = 0;
        rect.y = 0;
        rect.width = static_cast<NSUInteger>(targetWidth);
        rect.height = static_cast<NSUInteger>(targetHeight);
    }
    [encoder setScissorRect:rect];
}

// Convert a DrawPrimitive's positions+color/coverage into the CompactSolidVertex
// stream expected by the solid pipeline. Prefers precomputed compactVertices.
void buildSolidVertices(const wsc::DrawPrimitive &prim, std::vector<wsc::CompactSolidVertex> &out)
{
    if (!prim.compactVertices.empty()) {
        out = prim.compactVertices;
        return;
    }
    const std::size_t vertexCount = prim.positions.size() / 2;
    out.resize(vertexCount);
    const bool hasPackedColors = !prim.packedColors.empty()
                                  && prim.packedColors.size() >= vertexCount * 4;
    const bool hasFloatColors = !prim.colors.empty() && prim.colors.size() >= vertexCount * 4;
    const bool hasPackedCoverage = !prim.packedCoverage.empty()
                                    && prim.packedCoverage.size() >= vertexCount;
    const bool hasFloatCoverage = !prim.coverage.empty() && prim.coverage.size() >= vertexCount;

    std::uint32_t defaultColor;
    {
        auto clamp8 = [](float v) {
            v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
            return static_cast<std::uint32_t>(v * 255.0f + 0.5f);
        };
        defaultColor = clamp8(prim.color[0])
                     | (clamp8(prim.color[1]) << 8)
                     | (clamp8(prim.color[2]) << 16)
                     | (clamp8(prim.color[3]) << 24);
    }

    for (std::size_t i = 0; i < vertexCount; ++i) {
        wsc::CompactSolidVertex &v = out[i];
        v.x = prim.positions[i * 2 + 0];
        v.y = prim.positions[i * 2 + 1];
        if (hasPackedColors) {
            const std::uint8_t r = prim.packedColors[i * 4 + 0];
            const std::uint8_t g = prim.packedColors[i * 4 + 1];
            const std::uint8_t b = prim.packedColors[i * 4 + 2];
            const std::uint8_t a = prim.packedColors[i * 4 + 3];
            v.color = static_cast<std::uint32_t>(r)
                    | (static_cast<std::uint32_t>(g) << 8)
                    | (static_cast<std::uint32_t>(b) << 16)
                    | (static_cast<std::uint32_t>(a) << 24);
        } else if (hasFloatColors) {
            auto clamp8 = [](float f) {
                f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
                return static_cast<std::uint32_t>(f * 255.0f + 0.5f);
            };
            v.color = clamp8(prim.colors[i * 4 + 0])
                    | (clamp8(prim.colors[i * 4 + 1]) << 8)
                    | (clamp8(prim.colors[i * 4 + 2]) << 16)
                    | (clamp8(prim.colors[i * 4 + 3]) << 24);
        } else {
            v.color = defaultColor;
        }
        if (hasPackedCoverage) {
            v.coverage = prim.packedCoverage[i];
        } else if (hasFloatCoverage) {
            const float c = prim.coverage[i];
            v.coverage = static_cast<std::uint8_t>(std::max(0.0f, std::min(1.0f, c)) * 255.0f + 0.5f);
        } else {
            v.coverage = 255;
        }
    }
}

} // namespace

// -----------------------------------------------------------------------------
// Render target creation.
std::unique_ptr<IRenderTarget> MetalRenderDevice::createRenderTarget(int width, int height) const
{
    if (!context_ || !context_->deviceReady || width <= 0 || height <= 0) {
        return nullptr;
    }
    MTLTextureDescriptor *desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = preferredTextureStorageMode(context_->device);
    id<MTLTexture> texture = [context_->device newTextureWithDescriptor:desc];
    if (texture == nil) {
        return nullptr;
    }
    auto res = std::make_shared<MetalTextureResource>(texture, width, height, /*alpha=*/false, /*owned=*/true);
    context_->renderTargetCount += 1;
    context_->imageTextureCount += 1;
    return std::make_unique<MetalRenderTarget>(texture, res, width, height);
}

// -----------------------------------------------------------------------------
// Image resource creation.
SharedImageResource MetalRenderDevice::createImageResourceRGBA(int width, int height,
                                                               const std::vector<unsigned char> &pixels) const
{
    if (!context_ || !context_->deviceReady || width <= 0 || height <= 0) {
        return {};
    }
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (pixels.size() < expected) {
        return {};
    }
    MTLTextureDescriptor *desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = preferredTextureStorageMode(context_->device);
    id<MTLTexture> texture = [context_->device newTextureWithDescriptor:desc];
    if (texture == nil) {
        return {};
    }
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture replaceRegion:region mipmapLevel:0 withBytes:pixels.data()
               bytesPerRow:static_cast<NSUInteger>(width) * 4];
    context_->imageTextureCount += 1;
    return std::make_shared<MetalTextureResource>(texture, width, height, /*alpha=*/false, /*owned=*/true);
}

SharedImageResource MetalRenderDevice::createImageResourceAlpha8(int width, int height,
                                                                 const std::vector<unsigned char> &pixels) const
{
    if (!context_ || !context_->deviceReady || width <= 0 || height <= 0) {
        return {};
    }
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixels.size() < expected) {
        return {};
    }
    MTLTextureDescriptor *desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = preferredTextureStorageMode(context_->device);
    id<MTLTexture> texture = [context_->device newTextureWithDescriptor:desc];
    if (texture == nil) {
        return {};
    }
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture replaceRegion:region mipmapLevel:0 withBytes:pixels.data()
               bytesPerRow:static_cast<NSUInteger>(width)];
    context_->imageTextureCount += 1;
    return std::make_shared<MetalTextureResource>(texture, width, height, /*alpha=*/true, /*owned=*/true);
}

SharedImageResource MetalRenderDevice::createImageResourceFromImageData(int width, int height, int channels,
                                                                        const unsigned char *pixels,
                                                                        bool /*generateMipmaps*/) const
{
    if (!context_ || !context_->deviceReady || width <= 0 || height <= 0 || pixels == nullptr) {
        return {};
    }
    // Expand RGB to RGBA if needed (Metal has no widely-supported RGB8 storage).
    std::vector<unsigned char> rgba;
    const unsigned char *source = pixels;
    if (channels == 4) {
        // OK as-is.
    } else if (channels == 3) {
        rgba.resize(static_cast<std::size_t>(width) * height * 4u);
        for (std::size_t i = 0, n = static_cast<std::size_t>(width) * height; i < n; ++i) {
            rgba[i * 4 + 0] = pixels[i * 3 + 0];
            rgba[i * 4 + 1] = pixels[i * 3 + 1];
            rgba[i * 4 + 2] = pixels[i * 3 + 2];
            rgba[i * 4 + 3] = 0xff;
        }
        source = rgba.data();
    } else if (channels == 1) {
        return createImageResourceAlpha8(width, height,
                                         std::vector<unsigned char>(pixels,
                                                                    pixels + static_cast<std::size_t>(width) * height));
    } else {
        return {};
    }
    std::vector<unsigned char> owned(source, source + static_cast<std::size_t>(width) * height * 4u);
    return createImageResourceRGBA(width, height, owned);
}

bool MetalRenderDevice::updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width,
                                                int height, const unsigned char *pixels,
                                                bool /*regenerateMipmaps*/) const
{
    auto *res = dynamic_cast<MetalTextureResource *>(imageResource.get());
    if (res == nullptr || !res->isValid() || pixels == nullptr) {
        return false;
    }
    return res->updateRGBA(x, y, width, height, pixels, false);
}

bool MetalRenderDevice::updateImageResourceAlpha8(const SharedImageResource &imageResource, int x, int y, int width,
                                                  int height, const unsigned char *pixels) const
{
    auto *res = dynamic_cast<MetalTextureResource *>(imageResource.get());
    if (res == nullptr || !res->isValid() || pixels == nullptr) {
        return false;
    }
    id<MTLTexture> texture = res->metalTexture();
    if (texture == nil) {
        return false;
    }
    if (x < 0 || y < 0 || x + width > res->width() || y + height > res->height()) {
        return false;
    }
    MTLRegion region = MTLRegionMake2D(x, y, width, height);
    [texture replaceRegion:region mipmapLevel:0 withBytes:pixels
               bytesPerRow:static_cast<NSUInteger>(width)];
    return true;
}

SharedImageResource MetalRenderDevice::wrapExternalImageResource(ImageResourceHandle handle) const
{
    if (!context_ || !context_->deviceReady || !handle.isValid()) {
        return {};
    }
    id<MTLTexture> texture = (__bridge id<MTLTexture>)(void *)handle.value;
    if (texture == nil) {
        return {};
    }
    return std::make_shared<MetalTextureResource>(
        texture,
        static_cast<int>(texture.width),
        static_cast<int>(texture.height),
        texture.pixelFormat == MTLPixelFormatR8Unorm,
        /*owned=*/false);
}

ImageResourceHandle MetalRenderDevice::nativeImageHandle(const SharedImageResource &resource) const
{
    auto *res = dynamic_cast<MetalTextureResource *>(resource.get());
    if (res == nullptr || !res->isValid()) {
        return {};
    }
    return res->nativeHandle();
}

// -----------------------------------------------------------------------------
// Stats.
RenderResourceStats MetalRenderDevice::resourceStats() const
{
    RenderResourceStats s{};
    if (context_) {
        s.imageTextureCount = context_->imageTextureCount;
        s.renderTargetCount = context_->renderTargetCount;
    }
    return s;
}

// -----------------------------------------------------------------------------
// Clip mask resource. Stores the coverage-annotated path geometry passed from
// the Canvas layer; the actual R8 mask texture is baked lazily inside
// executeCommands's createClipMaskTexture callback so the target size can
// track the current frame's canvas dimensions.
SharedClipMaskResource MetalRenderDevice::createClipMaskResource(const ClipMaskPath &maskPath) const
{
    if (!context_ || !context_->deviceReady) {
        return {};
    }
    if (maskPath.points.size() < 6) {
        return {};
    }
    return std::make_shared<MetalClipMaskResource>(maskPath.points, maskPath.coverage, maskPath.transform);
}

// -----------------------------------------------------------------------------
// Clip-mask rasterization. The Canvas layer accumulates coverage-annotated
// path resources via createClipMaskResource; the encoder then asks us for a
// combined mask texture the size of the current canvas whenever it needs to
// emit a ClipFill primitive. We walk each stored resource, transform its
// canvas-space points into Y-down NDC (matching the Solid pipeline), and draw
// them with the Solid pipeline into a fresh RGBA8 target. The ClipFill
// fragment shader downstream samples the red channel.
SharedImageResource MetalRenderDevice::rasterizeClipMask(const ClipMaskState &state,
                                                         int canvasWidth, int canvasHeight) const
{
    if (!context_ || !context_->deviceReady || state.resources.empty()
        || canvasWidth <= 0 || canvasHeight <= 0) {
        return {};
    }

    id<MTLTexture> maskTex = nil;
    @autoreleasepool {
        MTLTextureDescriptor *desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:static_cast<NSUInteger>(canvasWidth)
                                                              height:static_cast<NSUInteger>(canvasHeight)
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.storageMode = preferredTextureStorageMode(context_->device);
        maskTex = [context_->device newTextureWithDescriptor:desc];
        if (maskTex == nil) {
            return {};
        }

        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = maskTex;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

        id<MTLCommandBuffer> cb = [context_->commandQueue commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
        [enc setViewport:makeViewport(canvasWidth, canvasHeight)];

        // Blend mode 0 = SrcOver; each path's rasterization writes its
        // coverage into the red channel where 1 - dst.a is transparent. For
        // multi-clip intersection callers should compose by chaining a second
        // pass (follow-up); the current path handles the common single-clip
        // path used by clipRect / clipPath.
        id<MTLRenderPipelineState> pso = obtainPipeline(*context_, MetalPipelineKind::Solid, 0);
        if (pso == nil) {
            [enc endEncoding];
            [cb commit];
            return {};
        }
        [enc setRenderPipelineState:pso];
        MTLScissorRect fullRect;
        fullRect.x = 0;
        fullRect.y = 0;
        fullRect.width = static_cast<NSUInteger>(canvasWidth);
        fullRect.height = static_cast<NSUInteger>(canvasHeight);
        [enc setScissorRect:fullRect];

        for (const SharedClipMaskResource &resource : state.resources) {
            const auto *mres = dynamic_cast<const MetalClipMaskResource *>(resource.get());
            if (mres == nullptr || !mres->isValid()) {
                continue;
            }
            const std::vector<float> &points = mres->points();
            const std::vector<float> &coverage = mres->coverage();
            const glm::mat4 &tf = mres->transform();
            const std::size_t vcount = points.size() / 2;
            const bool hasCoverage = coverage.size() >= vcount;

            std::vector<wsc::CompactSolidVertex> verts;
            verts.reserve(vcount);
            const float widthF = static_cast<float>(canvasWidth);
            const float heightF = static_cast<float>(canvasHeight);
            for (std::size_t i = 0; i < vcount; ++i) {
                glm::vec4 p = tf * glm::vec4(points[i * 2 + 0], points[i * 2 + 1], 0.0f, 1.0f);
                const float ndcX = (p.x / widthF) * 2.0f - 1.0f;
                const float ndcY = (p.y / heightF) * 2.0f - 1.0f;
                wsc::CompactSolidVertex v;
                v.x = ndcX;
                v.y = ndcY;
                v.color = 0xFFFFFFFFu;
                if (hasCoverage) {
                    const float c = std::min(1.0f, std::max(0.0f, coverage[i]));
                    v.coverage = static_cast<std::uint8_t>(c * 255.0f + 0.5f);
                } else {
                    v.coverage = 255;
                }
                verts.push_back(v);
            }
            if (verts.empty()) {
                continue;
            }

            const std::size_t vertexBytes = verts.size() * sizeof(wsc::CompactSolidVertex);
            if (vertexBytes <= 4096) {
                [enc setVertexBytes:verts.data() length:vertexBytes atIndex:0];
            } else {
                id<MTLBuffer> vb = [context_->device newBufferWithBytes:verts.data()
                                                                 length:vertexBytes
                                                                options:MTLResourceStorageModeShared];
                [enc setVertexBuffer:vb offset:0 atIndex:0];
            }
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts.size()];
        }
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
    }

    if (maskTex == nil) {
        return {};
    }
    context_->imageTextureCount += 1;
    return std::make_shared<MetalTextureResource>(maskTex, canvasWidth, canvasHeight,
                                                  /*alpha=*/false, /*owned=*/true);
}

// -----------------------------------------------------------------------------
// Draw list execution.
namespace {

struct MetalExecutionStats
{
    std::size_t drawCallCount = 0;
    std::size_t mergedBatchCount = 0;
    std::size_t vertexBytes = 0;
    std::size_t indexBytes = 0;
    std::size_t packetCount = 0;
};

id<MTLTexture> alphaAtlasTexture(const wsc::DrawPrimitive &prim)
{
    if (!prim.texture) {
        return nil;
    }
    auto *res = dynamic_cast<const MetalTextureResource *>(prim.texture.get());
    return res ? res->metalTexture() : nil;
}

id<MTLTexture> clipMaskTexture(const wsc::DrawPrimitive &prim)
{
    if (!prim.clipTexture) {
        return nil;
    }
    auto *res = dynamic_cast<const MetalTextureResource *>(prim.clipTexture.get());
    return res ? res->metalTexture() : nil;
}

// Encode a single solid primitive.
void encodeSolid(id<MTLRenderCommandEncoder> encoder, MetalRenderDevice::MetalContext &ctx,
                 const wsc::DrawPrimitive &prim, int targetW, int targetH,
                 MetalExecutionStats &stats)
{
    std::vector<wsc::CompactSolidVertex> vertices;
    buildSolidVertices(prim, vertices);
    if (vertices.empty()) {
        return;
    }
    id<MTLRenderPipelineState> pso = obtainPipeline(ctx, MetalPipelineKind::Solid, prim.blendMode);
    if (pso == nil) {
        return;
    }
    [encoder setRenderPipelineState:pso];
    applyScissor(encoder, prim, targetW, targetH);

    const std::size_t vertexBytes = vertices.size() * sizeof(wsc::CompactSolidVertex);
    // setVertexBytes has a 4 KB limit; use a real buffer for larger uploads.
    if (vertexBytes <= 4096) {
        [encoder setVertexBytes:vertices.data() length:vertexBytes atIndex:0];
    } else {
        id<MTLBuffer> buf = [ctx.device newBufferWithBytes:vertices.data()
                                                    length:vertexBytes
                                                   options:MTLResourceStorageModeShared];
        [encoder setVertexBuffer:buf offset:0 atIndex:0];
    }
    stats.vertexBytes += vertexBytes;

    if (!prim.shortIndices.empty()) {
        const std::size_t byteCount = prim.shortIndices.size() * sizeof(std::uint16_t);
        id<MTLBuffer> ib = [ctx.device newBufferWithBytes:prim.shortIndices.data()
                                                   length:byteCount
                                                  options:MTLResourceStorageModeShared];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:prim.shortIndices.size()
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:ib
                     indexBufferOffset:0];
        stats.indexBytes += byteCount;
    } else if (!prim.indices.empty()) {
        const std::size_t byteCount = prim.indices.size() * sizeof(std::uint32_t);
        id<MTLBuffer> ib = [ctx.device newBufferWithBytes:prim.indices.data()
                                                   length:byteCount
                                                  options:MTLResourceStorageModeShared];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:prim.indices.size()
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:ib
                     indexBufferOffset:0];
        stats.indexBytes += byteCount;
    } else {
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertices.size()];
    }
    stats.drawCallCount += 1;
}

struct MetalTexturedUniforms
{
    float tint[4];
    float params[4];
};

void encodeTextured(id<MTLRenderCommandEncoder> encoder, MetalRenderDevice::MetalContext &ctx,
                    const wsc::DrawPrimitive &prim, int targetW, int targetH,
                    MetalExecutionStats &stats)
{
    id<MTLTexture> tex = alphaAtlasTexture(prim);
    if (tex == nil) {
        return;
    }
    const bool alphaOnly = tex.pixelFormat == MTLPixelFormatR8Unorm;

    // Build vertex stream: full-target quad by default, or explicit positions.
    // Layout matches the shader's TexturedVSInput (pos, uv, per-vertex tint).
    struct V { float px, py, u, v; std::uint32_t tint; };
    std::vector<V> vertices;
    const bool primHasPackedTints = !prim.packedTints.empty();
    if (!prim.texturedInstances.empty()) {
        vertices.reserve(prim.texturedInstances.size() * 6);
        for (std::size_t idx = 0; idx < prim.texturedInstances.size(); ++idx) {
            const wsc::TexturedQuadInstance &inst = prim.texturedInstances[idx];
            const std::uint32_t instTint = primHasPackedTints && idx < prim.packedTints.size()
                                               ? prim.packedTints[idx]
                                               : inst.packedTint;
            const float x0 = (inst.x0 / static_cast<float>(targetW)) * 2.0f - 1.0f;
            const float x1 = (inst.x1 / static_cast<float>(targetW)) * 2.0f - 1.0f;
            const float y0 = (inst.y0 / static_cast<float>(targetH)) * 2.0f - 1.0f;
            const float y1 = (inst.y1 / static_cast<float>(targetH)) * 2.0f - 1.0f;
            vertices.push_back({x0, y0, inst.u0, inst.v0, instTint});
            vertices.push_back({x1, y0, inst.u1, inst.v0, instTint});
            vertices.push_back({x1, y1, inst.u1, inst.v1, instTint});
            vertices.push_back({x0, y0, inst.u0, inst.v0, instTint});
            vertices.push_back({x1, y1, inst.u1, inst.v1, instTint});
            vertices.push_back({x0, y1, inst.u0, inst.v1, instTint});
        }
    } else if (!prim.positions.empty()) {
        const std::size_t count = prim.positions.size() / 2u;
        vertices.reserve(count);
        const bool hasUVs = prim.uvs.size() == prim.positions.size();
        const bool hasVertexTints = primHasPackedTints && prim.packedTints.size() >= count;
        for (std::size_t i = 0; i < count; ++i) {
            V v;
            v.px = prim.positions[i * 2 + 0];
            v.py = prim.positions[i * 2 + 1];
            v.u = hasUVs ? prim.uvs[i * 2 + 0] : (i == 1 || i == 2 || i == 4 ? 1.0f : 0.0f);
            v.v = hasUVs ? prim.uvs[i * 2 + 1] : (i == 2 || i == 4 || i == 5 ? 1.0f : 0.0f);
            v.tint = hasVertexTints ? prim.packedTints[i] : 0xffffffffu;
            vertices.push_back(v);
        }
    } else {
        // Full-target quad in NDC (Y-up before shader flip).
        vertices = {
            {-1.0f, -1.0f, 0.0f, 1.0f, 0xffffffffu},
            { 1.0f, -1.0f, 1.0f, 1.0f, 0xffffffffu},
            { 1.0f,  1.0f, 1.0f, 0.0f, 0xffffffffu},
            {-1.0f, -1.0f, 0.0f, 1.0f, 0xffffffffu},
            { 1.0f,  1.0f, 1.0f, 0.0f, 0xffffffffu},
            {-1.0f,  1.0f, 0.0f, 0.0f, 0xffffffffu},
        };
    }
    if (vertices.empty()) {
        return;
    }

    id<MTLRenderPipelineState> pso = obtainPipeline(
        ctx, alphaOnly ? MetalPipelineKind::TexturedAlpha : MetalPipelineKind::Textured, prim.blendMode);
    if (pso == nil) {
        return;
    }
    [encoder setRenderPipelineState:pso];
    applyScissor(encoder, prim, targetW, targetH);

    const std::size_t vertexBytes = vertices.size() * sizeof(V);
    if (vertexBytes <= 4096) {
        [encoder setVertexBytes:vertices.data() length:vertexBytes atIndex:0];
    } else {
        id<MTLBuffer> buf = [ctx.device newBufferWithBytes:vertices.data()
                                                    length:vertexBytes
                                                   options:MTLResourceStorageModeShared];
        [encoder setVertexBuffer:buf offset:0 atIndex:0];
    }
    stats.vertexBytes += vertexBytes;

    MetalTexturedUniforms u{};
    u.tint[0] = prim.tint[0];
    u.tint[1] = prim.tint[1];
    u.tint[2] = prim.tint[2];
    u.tint[3] = prim.tint[3];
    u.params[0] = prim.layerAlpha;
    u.params[1] = 0.0f;
    u.params[2] = 0.0f;
    u.params[3] = 0.0f;
    [encoder setFragmentBytes:&u length:sizeof(u) atIndex:0];

    [encoder setFragmentTexture:tex atIndex:0];
    id<MTLSamplerState> samp = obtainSampler(ctx, prim.sampling, prim.tileMode);
    [encoder setFragmentSamplerState:samp atIndex:0];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertices.size()];
    stats.drawCallCount += 1;
}

struct MetalGradientUniforms
{
    float params[4];
    float linearStart[4];
    float linearEnd[4];
    float radialCenter[4];
    float stops[8][4];
    float offsets[4];
    float offsetsHigh[4];
};

void encodeGradient(id<MTLRenderCommandEncoder> encoder, MetalRenderDevice::MetalContext &ctx,
                    const wsc::DrawPrimitive &prim, int targetW, int targetH,
                    MetalExecutionStats &stats)
{
    if (prim.positions.empty() || prim.localPositions.empty()) {
        return;
    }
    if (prim.positions.size() != prim.localPositions.size()) {
        return;
    }
    id<MTLRenderPipelineState> pso = obtainPipeline(ctx, MetalPipelineKind::Gradient, prim.blendMode);
    if (pso == nil) {
        return;
    }

    struct V { float px, py, lx, ly; };
    const std::size_t count = prim.positions.size() / 2u;
    std::vector<V> vertices;
    vertices.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        V v;
        v.px = prim.positions[i * 2 + 0];
        v.py = prim.positions[i * 2 + 1];
        v.lx = prim.localPositions[i * 2 + 0];
        v.ly = prim.localPositions[i * 2 + 1];
        vertices.push_back(v);
    }

    [encoder setRenderPipelineState:pso];
    applyScissor(encoder, prim, targetW, targetH);

    const std::size_t vertexBytes = vertices.size() * sizeof(V);
    if (vertexBytes <= 4096) {
        [encoder setVertexBytes:vertices.data() length:vertexBytes atIndex:0];
    } else {
        id<MTLBuffer> buf = [ctx.device newBufferWithBytes:vertices.data()
                                                    length:vertexBytes
                                                   options:MTLResourceStorageModeShared];
        [encoder setVertexBuffer:buf offset:0 atIndex:0];
    }
    stats.vertexBytes += vertexBytes;

    MetalGradientUniforms u{};
    u.params[0] = static_cast<float>(prim.gradientType);
    u.params[1] = static_cast<float>(prim.gradientTileMode);
    u.params[2] = static_cast<float>(std::min(prim.gradientStopCount, 8));
    u.params[3] = prim.radialRadius;
    u.linearStart[0] = prim.linearStart[0];
    u.linearStart[1] = prim.linearStart[1];
    u.linearEnd[0] = prim.linearEnd[0];
    u.linearEnd[1] = prim.linearEnd[1];
    u.radialCenter[0] = prim.radialCenter[0];
    u.radialCenter[1] = prim.radialCenter[1];
    const int n = std::min(prim.gradientStopCount, 8);
    for (int i = 0; i < n; ++i) {
        u.stops[i][0] = prim.gradientStopColors[i * 4 + 0];
        u.stops[i][1] = prim.gradientStopColors[i * 4 + 1];
        u.stops[i][2] = prim.gradientStopColors[i * 4 + 2];
        u.stops[i][3] = prim.gradientStopColors[i * 4 + 3];
    }
    for (int i = 0; i < 4; ++i) {
        u.offsets[i] = i < n ? prim.gradientStopPositions[i] : 1.0f;
    }
    for (int i = 0; i < 4; ++i) {
        u.offsetsHigh[i] = (i + 4) < n ? prim.gradientStopPositions[i + 4] : 1.0f;
    }
    [encoder setFragmentBytes:&u length:sizeof(u) atIndex:0];

    if (!prim.shortIndices.empty()) {
        const std::size_t byteCount = prim.shortIndices.size() * sizeof(std::uint16_t);
        id<MTLBuffer> ib = [ctx.device newBufferWithBytes:prim.shortIndices.data()
                                                   length:byteCount
                                                  options:MTLResourceStorageModeShared];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:prim.shortIndices.size()
                             indexType:MTLIndexTypeUInt16
                           indexBuffer:ib
                     indexBufferOffset:0];
        stats.indexBytes += byteCount;
    } else if (!prim.indices.empty()) {
        const std::size_t byteCount = prim.indices.size() * sizeof(std::uint32_t);
        id<MTLBuffer> ib = [ctx.device newBufferWithBytes:prim.indices.data()
                                                   length:byteCount
                                                  options:MTLResourceStorageModeShared];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:prim.indices.size()
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:ib
                     indexBufferOffset:0];
        stats.indexBytes += byteCount;
    } else {
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertices.size()];
    }
    stats.drawCallCount += 1;
}

struct MetalClipUniforms
{
    float color[4];
    float uvScaleOffset[4];
};

void encodeClipFill(id<MTLRenderCommandEncoder> encoder, MetalRenderDevice::MetalContext &ctx,
                    const wsc::DrawPrimitive &prim, int targetW, int targetH,
                    MetalExecutionStats &stats)
{
    id<MTLTexture> mask = alphaAtlasTexture(prim);
    if (mask == nil) {
        return;
    }
    id<MTLRenderPipelineState> pso = obtainPipeline(ctx, MetalPipelineKind::ClipFill, prim.blendMode);
    if (pso == nil) {
        return;
    }
    [encoder setRenderPipelineState:pso];
    applyScissor(encoder, prim, targetW, targetH);

    // Full-target quad in NDC (Y-up pre-flip).
    const float verts[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f,
    };
    [encoder setVertexBytes:verts length:sizeof(verts) atIndex:0];
    stats.vertexBytes += sizeof(verts);

    MetalClipUniforms u{};
    u.color[0] = prim.color[0];
    u.color[1] = prim.color[1];
    u.color[2] = prim.color[2];
    u.color[3] = prim.color[3];
    u.uvScaleOffset[0] = 1.0f;
    u.uvScaleOffset[1] = 1.0f;
    u.uvScaleOffset[2] = 0.0f;
    u.uvScaleOffset[3] = 0.0f;
    // Only the fragment stage consumes ClipUniforms; the vertex shader bakes
    // the identity UV transform in directly so its buffer(0) slot stays free
    // for the vertex data attached via the stage_in vertex descriptor.
    [encoder setFragmentBytes:&u length:sizeof(u) atIndex:0];

    [encoder setFragmentTexture:mask atIndex:0];
    [encoder setFragmentSamplerState:ctx.clipSampler atIndex:0];

    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    stats.drawCallCount += 1;
}

} // namespace

bool MetalRenderDevice::executeDrawList(const std::unique_ptr<IRenderTarget> &target,
                                        const wsc::DrawList &drawList) const
{
    if (!context_ || !context_->deviceReady || !target) {
        return false;
    }
    auto *rt = dynamic_cast<MetalRenderTarget *>(target.get());
    if (rt == nullptr || !rt->isValid()) {
        return false;
    }
    if (drawList.empty()) {
        // Nothing to do; per the contract, treat as a success so callers keep
        // the target ready for readback.
        return true;
    }

    MetalExecutionStats stats;
    stats.packetCount = drawList.size();

    @autoreleasepool {
        // The public Canvas contract begins each frame with a fully transparent
        // clear before commands replay, matching OpenGL/Vulkan.
        MTLClearColor clear = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
        MTLLoadAction loadAction = rt->hasPending() ? MTLLoadActionClear : MTLLoadActionClear;

        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = rt->metalTexture();
        pass.colorAttachments[0].loadAction = loadAction;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = clear;

        id<MTLCommandBuffer> cb = [context_->commandQueue commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
        [enc setViewport:makeViewport(rt->width(), rt->height())];

        for (const wsc::DrawPrimitive &prim : drawList) {
            switch (prim.kind) {
            case wsc::DrawPrimitiveKind::SolidTriangles:
                encodeSolid(enc, *context_, prim, rt->width(), rt->height(), stats);
                break;
            case wsc::DrawPrimitiveKind::TexturedQuad:
                encodeTextured(enc, *context_, prim, rt->width(), rt->height(), stats);
                break;
            case wsc::DrawPrimitiveKind::GradientFill:
                encodeGradient(enc, *context_, prim, rt->width(), rt->height(), stats);
                break;
            case wsc::DrawPrimitiveKind::ClipFill:
                encodeClipFill(enc, *context_, prim, rt->width(), rt->height(), stats);
                break;
            }
        }
        [enc endEncoding];

        // On managed storage we need an explicit synchronize-resource blit to
        // make the CPU-visible copy up to date before readPixelsRGBA runs.
#if !TARGET_OS_IPHONE && !TARGET_OS_TV
        if (rt->metalTexture().storageMode == MTLStorageModeManaged) {
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit synchronizeResource:rt->metalTexture()];
            [blit endEncoding];
        }
#endif
        [cb commit];
        [cb waitUntilCompleted];
    }

    lastExecutionDrawCallCount_ = stats.drawCallCount;
    lastExecutionMergedBatchCount_ = stats.drawCallCount;
    lastCompiledPacketCount_ = stats.packetCount;
    lastCompiledVertexBytes_ = stats.vertexBytes;
    lastCompiledIndexBytes_ = stats.indexBytes;

    // Remember the target for the next readPixelsRGBA call. Callers routinely
    // pair executeCommands + readPixelsRGBA back-to-back for offscreen work.
    context_->lastReadbackTexture = rt->metalTexture();
    context_->lastReadbackWidth = rt->width();
    context_->lastReadbackHeight = rt->height();
    return true;
}

// -----------------------------------------------------------------------------
// Command execution.
bool MetalRenderDevice::executeCommands(const std::unique_ptr<IRenderTarget> &target,
                                        const std::vector<std::unique_ptr<Command>> &commands,
                                        const OffscreenRenderRequest &request) const
{
    if (!context_ || !context_->deviceReady || !target) {
        return false;
    }
    auto *rt = dynamic_cast<MetalRenderTarget *>(target.get());
    if (rt == nullptr || !rt->isValid()) {
        return false;
    }

    if (!rt->hasPending()) {
        rt->begin(request);
    }

    // Encode commands into a backend-neutral draw list.
    CommandDrawListEncodeRequest encodeRequest;
    encodeRequest.canvasWidth = rt->width();
    encodeRequest.canvasHeight = rt->height();
    encodeRequest.targetHeight = rt->height();
    encodeRequest.scissorOffsetX = 0;
    encodeRequest.scissorOffsetY = 0;
    encodeRequest.createClipMaskTexture =
        [this](const ClipMaskState &state, int width, int height) -> SharedImageResource {
            return rasterizeClipMask(state, width, height);
        };

    wsc::DrawList drawList;
    std::string encodeError;
    if (!encodeCommandsToDrawList(commands, encodeRequest, drawList, &encodeError)) {
        WSC_LOG_ERROR("MetalRenderDevice",
                      (std::string("Failed to encode commands: ") + encodeError).c_str());
        return false;
    }
    const bool ok = executeDrawList(target, drawList);
    rt->end();
    return ok;
}

SharedImageResource MetalRenderDevice::renderCommandsToImageResource(
    const std::vector<std::unique_ptr<Command>> &commands, const OffscreenRenderRequest &request) const
{
    if (!context_ || !context_->deviceReady) {
        return {};
    }
    const int width = request.targetWidth > 0 ? request.targetWidth
                                              : (request.canvasWidth > 0 ? request.canvasWidth : 1);
    const int height = request.targetHeight > 0 ? request.targetHeight
                                                : (request.canvasHeight > 0 ? request.canvasHeight : 1);
    std::unique_ptr<IRenderTarget> target = createRenderTarget(width, height);
    if (target == nullptr) {
        return {};
    }
    if (!target->begin(request)) {
        return {};
    }
    target->activate();
    if (!executeCommands(target, commands, request)) {
        return {};
    }
    return target->getImageResource();
}

// -----------------------------------------------------------------------------
// Gaussian blur (separable two-pass).
namespace {

constexpr int kMetalBlurMaxTaps = 31;

struct MetalBlurUniforms
{
    float direction[4];    // xy = per-pixel step, zw = padding
    int   tapCount = 0;
    int   tileMode = 0;
    float padding2[2] = {0.0f, 0.0f};
    float weights[32][4] = {}; // .x holds the weight; kMetalBlurMaxTaps + 1 slots
};

int computeGaussianWeights(float sigma, MetalBlurUniforms &out)
{
    if (sigma < 0.05f) {
        // Effectively identity.
        out.tapCount = 0;
        out.weights[0][0] = 1.0f;
        return 0;
    }
    const int radius = std::min(kMetalBlurMaxTaps,
                                std::max(1, static_cast<int>(std::ceil(sigma * 3.0f))));
    double sum = 0.0;
    std::vector<double> w(radius + 1);
    const double twoSigmaSq = 2.0 * static_cast<double>(sigma) * static_cast<double>(sigma);
    for (int i = 0; i <= radius; ++i) {
        const double d = static_cast<double>(i);
        w[i] = std::exp(-(d * d) / twoSigmaSq);
        sum += (i == 0 ? w[i] : 2.0 * w[i]);
    }
    if (sum <= 0.0) sum = 1.0;
    for (int i = 0; i <= radius; ++i) {
        out.weights[i][0] = static_cast<float>(w[i] / sum);
    }
    out.tapCount = radius;
    return radius;
}

id<MTLTexture> makeFilterTexture(id<MTLDevice> device, int width, int height,
                                 MTLTextureUsage usage)
{
    MTLTextureDescriptor *desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:NO];
    desc.usage = usage;
    desc.storageMode = preferredTextureStorageMode(device);
    return [device newTextureWithDescriptor:desc];
}

void encodeBlurPass(id<MTLCommandBuffer> cb,
                    MetalRenderDevice::MetalContext &ctx,
                    id<MTLTexture> src,
                    id<MTLTexture> dst,
                    const MetalBlurUniforms &u,
                    int width, int height)
{
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = dst;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
    [enc setViewport:makeViewport(width, height)];
    id<MTLRenderPipelineState> pso = obtainPipeline(ctx, MetalPipelineKind::Blur, /*blend=*/1 /*Src*/);
    if (pso == nil) {
        [enc endEncoding];
        return;
    }
    [enc setRenderPipelineState:pso];

    // Full-target quad in Y-down NDC + top-left UV mapping. The vertex shader
    // flips Y so (0,0) UV pairs up with the framebuffer top-left.
    const float verts[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };
    [enc setVertexBytes:verts length:sizeof(verts) atIndex:0];
    [enc setFragmentBytes:&u length:sizeof(MetalBlurUniforms) atIndex:0];
    [enc setFragmentTexture:src atIndex:0];
    [enc setFragmentSamplerState:ctx.clipSampler atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [enc endEncoding];
}

} // namespace

SharedImageResource MetalRenderDevice::filterImageResource(const SharedImageResource &source,
                                                           int width, int height,
                                                           const wsc::ImageFilter &filter,
                                                           FilterExecutionStats *executionStats) const
{
    if (executionStats != nullptr) {
        *executionStats = {};
    }
    if (!context_ || !context_->deviceReady || !source || width <= 0 || height <= 0) {
        return {};
    }
    // Only the plain Gaussian blur type is implemented on Metal so far. Other
    // filter kinds (inner shadow, color adjust, grain) fall through to an
    // empty return so the caller can degrade gracefully.
    if (filter.type() != wsc::ImageFilter::Type::Blur || !filter.isValid()) {
        return {};
    }
    auto *srcResource = dynamic_cast<const MetalTextureResource *>(source.get());
    if (srcResource == nullptr || !srcResource->isValid()) {
        return {};
    }

    // Radii are expressed in filter-target pixels; sigma follows the same
    // 3-sigma reach convention as `ImageFilter::blurSigma` (radius = 3σ).
    const float sigmaX = std::max(0.0f, filter.radiusX() / 3.0f);
    const float sigmaY = std::max(0.0f, filter.radiusY() / 3.0f);
    MetalBlurUniforms uX{};
    MetalBlurUniforms uY{};
    computeGaussianWeights(sigmaX, uX);
    computeGaussianWeights(sigmaY, uY);
    uX.direction[0] = 1.0f / static_cast<float>(width);
    uX.direction[1] = 0.0f;
    uY.direction[0] = 0.0f;
    uY.direction[1] = 1.0f / static_cast<float>(height);
    const int tileMode = filter.tileMode() == wsc::ImageFilter::TileMode::Decal ? 1 : 0;
    uX.tileMode = tileMode;
    uY.tileMode = tileMode;

    id<MTLTexture> tempTex = nil;
    id<MTLTexture> finalTex = nil;
    @autoreleasepool {
        tempTex = makeFilterTexture(context_->device, width, height,
                                    MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        finalTex = makeFilterTexture(context_->device, width, height,
                                     MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
        if (tempTex == nil || finalTex == nil) {
            return {};
        }

        id<MTLCommandBuffer> cb = [context_->commandQueue commandBuffer];
        encodeBlurPass(cb, *context_, srcResource->metalTexture(), tempTex, uX, width, height);
        encodeBlurPass(cb, *context_, tempTex, finalTex, uY, width, height);
        [cb commit];
        [cb waitUntilCompleted];
    }

    if (finalTex == nil) {
        return {};
    }
    if (executionStats != nullptr) {
        executionStats->passCount = 2;
        executionStats->pixelPassCount = 2;
        executionStats->downsampled = false;
    }
    context_->imageTextureCount += 1;
    return std::make_shared<MetalTextureResource>(finalTex, width, height,
                                                  /*alpha=*/false, /*owned=*/true);
}

// -----------------------------------------------------------------------------
// Readback.
bool MetalRenderDevice::readPixelsRGBA(int width, int height, std::vector<unsigned char> &pixels) const
{
    if (!context_ || !context_->deviceReady) {
        pixels.clear();
        return false;
    }
    id<MTLTexture> tex = context_->lastReadbackTexture;
    if (tex == nil || width != context_->lastReadbackWidth || height != context_->lastReadbackHeight) {
        pixels.clear();
        return false;
    }
    pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [tex getBytes:pixels.data()
      bytesPerRow:static_cast<NSUInteger>(width) * 4u
       fromRegion:region
      mipmapLevel:0];
    return true;
}

#else // !WHATSCANVAS_ENABLE_METAL || !__APPLE__

// Inert stub used when Metal support is not compiled in. Keeps the class
// referenceable so RenderDeviceFactory always has a symbol to link.
struct MetalRenderDevice::MetalContext
{
};

bool MetalRenderDevice::isAvailable() { return false; }

MetalRenderDevice::MetalRenderDevice()
    : context_(nullptr)
{
}

MetalRenderDevice::~MetalRenderDevice() = default;

void MetalRenderDevice::initializeBackend() {}
void MetalRenderDevice::finalizeBackend() {}
bool MetalRenderDevice::readPixelsRGBA(int, int, std::vector<unsigned char> &) const { return false; }
std::unique_ptr<IRenderTarget> MetalRenderDevice::createRenderTarget(int, int) const { return nullptr; }
SharedClipMaskResource MetalRenderDevice::createClipMaskResource(const ClipMaskPath &) const { return {}; }
SharedImageResource MetalRenderDevice::createImageResourceRGBA(int, int,
                                                               const std::vector<unsigned char> &) const { return {}; }
SharedImageResource MetalRenderDevice::createImageResourceAlpha8(int, int,
                                                                 const std::vector<unsigned char> &) const { return {}; }
SharedImageResource MetalRenderDevice::createImageResourceFromImageData(int, int, int, const unsigned char *,
                                                                        bool) const { return {}; }
bool MetalRenderDevice::updateImageResourceRGBA(const SharedImageResource &, int, int, int, int,
                                                const unsigned char *, bool) const { return false; }
bool MetalRenderDevice::updateImageResourceAlpha8(const SharedImageResource &, int, int, int, int,
                                                  const unsigned char *) const { return false; }
SharedImageResource MetalRenderDevice::wrapExternalImageResource(ImageResourceHandle) const { return {}; }
ImageResourceHandle MetalRenderDevice::nativeImageHandle(const SharedImageResource &) const { return {}; }
RenderResourceStats MetalRenderDevice::resourceStats() const { return {}; }
SharedImageResource MetalRenderDevice::renderCommandsToImageResource(
    const std::vector<std::unique_ptr<Command>> &, const OffscreenRenderRequest &) const { return {}; }
bool MetalRenderDevice::executeDrawList(const std::unique_ptr<IRenderTarget> &,
                                        const wsc::DrawList &) const { return false; }
bool MetalRenderDevice::executeCommands(const std::unique_ptr<IRenderTarget> &,
                                        const std::vector<std::unique_ptr<Command>> &,
                                        const OffscreenRenderRequest &) const { return false; }
bool MetalRenderDevice::isDeviceReady() const { return false; }
const std::string &MetalRenderDevice::selectedDeviceName() const
{
    static const std::string kEmpty;
    return kEmpty;
}

#endif // WHATSCANVAS_ENABLE_METAL && __APPLE__
