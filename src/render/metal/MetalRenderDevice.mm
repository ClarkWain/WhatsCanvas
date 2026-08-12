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
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#elif TARGET_OS_IPHONE || TARGET_OS_TV
#import <UIKit/UIKit.h>
#endif

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
    float4 tint;         // rgba multiplier
    float4 params;       // x = layerAlpha, y = roundedRadius (px), zw = quad size (px)
    // Row-major 4x4 color matrix + rgba offset. Applied to the sampled colour
    // (before tint/alpha) when useColorMatrix.x > 0.5. Rows stored as float4s.
    float4 useColorMatrix; // x flag, yzw unused
    float4 colorMatrixRow0;
    float4 colorMatrixRow1;
    float4 colorMatrixRow2;
    float4 colorMatrixRow3;
    float4 colorMatrixOffset;
};

vertex TexturedVSOut textured_vs(TexturedVSInput in [[stage_in]])
{
    TexturedVSOut out;
    out.position = float4(in.position.x, -in.position.y, 0.0, 1.0);
    out.uv = in.uv;
    out.vertexTint = float4(in.vertexTint) / 255.0;
    return out;
}

// Coverage of a rounded-rectangle mask: returns 1 fully inside, 0 outside the
// corner, and a smooth 1-pixel roll-off along the arc for cheap AA. The
// destination rect is (0,0)..(w,h) in local pixels; the same math applies at
// all four corners by symmetry.
static float roundedRectCoverage(float2 localPx, float2 sizePx, float radius)
{
    if (radius <= 0.5) return 1.0;
    float2 corner = min(localPx, sizePx - localPx);
    if (corner.x >= radius || corner.y >= radius) return 1.0;
    float2 d = float2(radius, radius) - corner;
    float dist = length(d);
    return saturate(radius + 0.5 - dist);
}

fragment float4 textured_fs(TexturedVSOut in [[stage_in]],
                            texture2d<float> tex [[texture(0)]],
                            sampler samp [[sampler(0)]],
                            constant TexturedUniforms &u [[buffer(0)]])
{
    float4 s = tex.sample(samp, in.uv);
    if (u.useColorMatrix.x > 0.5) {
        float4 m = float4(
            dot(u.colorMatrixRow0, s),
            dot(u.colorMatrixRow1, s),
            dot(u.colorMatrixRow2, s),
            dot(u.colorMatrixRow3, s));
        s = m + u.colorMatrixOffset;
    }
    float4 c = s * u.tint * in.vertexTint;
    c.a *= u.params.x;
    if (u.params.y > 0.5) {
        float2 sizePx = u.params.zw;
        float coverage = roundedRectCoverage(in.uv * sizePx, sizePx, u.params.y);
        c.a *= coverage;
    }
    return c;
}

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
    // Post-blur adjustment payload, active only when applyPost > 0.5. Saturation
    // 1 leaves the source untouched; brightness/contrast pivot around 0.5 grey.
    // Grain adds a small deterministic dither driven by the UV hash.
    float  applyPost;
    float  saturation;
    float4 colorAdjust;    // x brightness, y contrast, z grain amount, w unused
    float4 weights[32];    // weights[i].x used; kept as float4 for alignment
};

vertex BlurVSOut blur_vs(BlurVSInput in [[stage_in]])
{
    BlurVSOut out;
    out.position = float4(in.position.x, -in.position.y, 0.0, 1.0);
    out.uv = in.uv;
    return out;
}

static float4 applyBlurPost(float4 c, constant BlurUniforms &u, float2 uv)
{
    // Saturation.
    float luma = dot(c.rgb, float3(0.299, 0.587, 0.114));
    c.rgb = mix(float3(luma), c.rgb, u.saturation);
    // Brightness and contrast (about 0.5 pivot).
    c.rgb *= u.colorAdjust.x;
    c.rgb = (c.rgb - 0.5) * u.colorAdjust.y + 0.5;
    // Grain: deterministic per-fragment dither in [-0.5, 0.5] * amount.
    float noise = fract(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
    c.rgb += (noise - 0.5) * u.colorAdjust.z;
    return c;
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
    if (u.applyPost > 0.5) {
        acc = applyBlurPost(acc, u, in.uv);
    }
    return acc;
}

// ---- Mask multiply pipeline ----
// A full-target compose that reads two coverage textures and outputs their
// per-pixel product. Used by rasterizeClipMask to intersect stacked clip
// paths — plain multi-draw rasterisation cannot do this because pixels
// outside a shape never invoke the fragment shader, so the "outside second
// clip" region silently keeps the first clip's coverage. Compositing two
// separately rasterised masks with this shader forces every pixel through a
// fragment invocation.
fragment float4 mask_multiply_fs(BlurVSOut in [[stage_in]],
                                 texture2d<float> a [[texture(0)]],
                                 texture2d<float> b [[texture(1)]],
                                 sampler samp [[sampler(0)]])
{
    float ca = a.sample(samp, in.uv).r;
    float cb = b.sample(samp, in.uv).r;
    float c = ca * cb;
    return float4(c, c, c, c);
}

// ---- Inner shadow support ----
// filterImageResource(InnerShadow) is implemented as four full-target passes:
//   1. invert_alpha_fs: emit (1 - source.a) in every channel so the blur has
//      strong values inside "outside the silhouette" pixels and zero inside
//      the shape.
//   2. blur_fs (horizontal) on the inverted alpha.
//   3. blur_fs (vertical) on the horizontal result.
//   4. inner_shadow_compose_fs: sample the source and the blurred inverted
//      alpha (offset by the caller-supplied direction), then composite the
//      shadow inside the source silhouette with the requested colour.
fragment float4 invert_alpha_fs(BlurVSOut in [[stage_in]],
                                texture2d<float> src [[texture(0)]],
                                sampler samp [[sampler(0)]])
{
    float a = src.sample(samp, in.uv).a;
    float inv = 1.0 - a;
    return float4(inv, inv, inv, inv);
}

struct InnerShadowUniforms {
    float4 shadowColor;         // rgba in [0,1], premultiplied on the CPU side.
    float2 offsetUv;            // per-pixel offset applied when sampling the blur.
    float2 padding;
};

fragment float4 inner_shadow_compose_fs(BlurVSOut in [[stage_in]],
                                        texture2d<float> src [[texture(0)]],
                                        texture2d<float> blurredInverted [[texture(1)]],
                                        sampler samp [[sampler(0)]],
                                        constant InnerShadowUniforms &u [[buffer(0)]])
{
    float4 srcSample = src.sample(samp, in.uv);
    float shadow = blurredInverted.sample(samp, in.uv - u.offsetUv).r;
    // Restrict shadow to the silhouette and scale by the shadow colour alpha.
    float intensity = shadow * srcSample.a * u.shadowColor.a;
    float3 rgb = mix(srcSample.rgb, u.shadowColor.rgb, intensity);
    return float4(rgb, srcSample.a);
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
    Dst = 5,
    Clear = 6,
    SrcIn = 7,
    DstIn = 8,
    SrcOut = 9,
    DstOut = 10,
    SrcAtop = 11,
    DstAtop = 12,
    Xor = 13,
};

MetalBlendMode blendModeFromInt(int value)
{
    switch (value) {
    case 1: return MetalBlendMode::Src;
    case 2: return MetalBlendMode::Add;
    case 3: return MetalBlendMode::Multiply;
    case 4: return MetalBlendMode::Screen;
    case 5: return MetalBlendMode::Dst;
    case 6: return MetalBlendMode::Clear;
    case 7: return MetalBlendMode::SrcIn;
    case 8: return MetalBlendMode::DstIn;
    case 9: return MetalBlendMode::SrcOut;
    case 10: return MetalBlendMode::DstOut;
    case 11: return MetalBlendMode::SrcAtop;
    case 12: return MetalBlendMode::DstAtop;
    case 13: return MetalBlendMode::Xor;
    case 0:
    default:
        return MetalBlendMode::SrcOver;
    }
}

void configureBlend(MTLRenderPipelineColorAttachmentDescriptor *color, MetalBlendMode mode)
{
    color.blendingEnabled = YES;
    color.writeMask = MTLColorWriteMaskAll;
    color.rgbBlendOperation = MTLBlendOperationAdd;
    color.alphaBlendOperation = MTLBlendOperationAdd;
    switch (mode) {
    case MetalBlendMode::Src:
        color.blendingEnabled = NO;
        break;
    case MetalBlendMode::Add:
        color.sourceRGBBlendFactor = MTLBlendFactorOne;
        color.sourceAlphaBlendFactor = MTLBlendFactorOne;
        color.destinationRGBBlendFactor = MTLBlendFactorOne;
        color.destinationAlphaBlendFactor = MTLBlendFactorOne;
        break;
    case MetalBlendMode::Multiply:
        color.sourceRGBBlendFactor = MTLBlendFactorDestinationColor;
        color.sourceAlphaBlendFactor = MTLBlendFactorDestinationAlpha;
        color.destinationRGBBlendFactor = MTLBlendFactorZero;
        color.destinationAlphaBlendFactor = MTLBlendFactorZero;
        break;
    case MetalBlendMode::Screen:
        color.sourceRGBBlendFactor = MTLBlendFactorOne;
        color.sourceAlphaBlendFactor = MTLBlendFactorOne;
        color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceColor;
        color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case MetalBlendMode::Dst:
        // Keep destination untouched: src contributes nothing.
        color.sourceRGBBlendFactor = MTLBlendFactorZero;
        color.sourceAlphaBlendFactor = MTLBlendFactorZero;
        color.destinationRGBBlendFactor = MTLBlendFactorOne;
        color.destinationAlphaBlendFactor = MTLBlendFactorOne;
        break;
    case MetalBlendMode::Clear:
        color.sourceRGBBlendFactor = MTLBlendFactorZero;
        color.sourceAlphaBlendFactor = MTLBlendFactorZero;
        color.destinationRGBBlendFactor = MTLBlendFactorZero;
        color.destinationAlphaBlendFactor = MTLBlendFactorZero;
        break;
    case MetalBlendMode::SrcIn:
        // src * dst.a: source only where destination exists.
        color.sourceRGBBlendFactor = MTLBlendFactorDestinationAlpha;
        color.sourceAlphaBlendFactor = MTLBlendFactorDestinationAlpha;
        color.destinationRGBBlendFactor = MTLBlendFactorZero;
        color.destinationAlphaBlendFactor = MTLBlendFactorZero;
        break;
    case MetalBlendMode::DstIn:
        // dst * src.a: destination masked by source alpha.
        color.sourceRGBBlendFactor = MTLBlendFactorZero;
        color.sourceAlphaBlendFactor = MTLBlendFactorZero;
        color.destinationRGBBlendFactor = MTLBlendFactorSourceAlpha;
        color.destinationAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        break;
    case MetalBlendMode::SrcOut:
        // src * (1 - dst.a): source only where destination is transparent.
        color.sourceRGBBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
        color.sourceAlphaBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
        color.destinationRGBBlendFactor = MTLBlendFactorZero;
        color.destinationAlphaBlendFactor = MTLBlendFactorZero;
        break;
    case MetalBlendMode::DstOut:
        // dst * (1 - src.a): destination punched by source alpha.
        color.sourceRGBBlendFactor = MTLBlendFactorZero;
        color.sourceAlphaBlendFactor = MTLBlendFactorZero;
        color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case MetalBlendMode::SrcAtop:
        // src * dst.a + dst * (1 - src.a).
        color.sourceRGBBlendFactor = MTLBlendFactorDestinationAlpha;
        color.sourceAlphaBlendFactor = MTLBlendFactorDestinationAlpha;
        color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case MetalBlendMode::DstAtop:
        // src * (1 - dst.a) + dst * src.a.
        color.sourceRGBBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
        color.sourceAlphaBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
        color.destinationRGBBlendFactor = MTLBlendFactorSourceAlpha;
        color.destinationAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        break;
    case MetalBlendMode::Xor:
        // Symmetric difference: src * (1 - dst.a) + dst * (1 - src.a).
        color.sourceRGBBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
        color.sourceAlphaBlendFactor = MTLBlendFactorOneMinusDestinationAlpha;
        color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case MetalBlendMode::SrcOver:
    default:
        // Standard premultiplied alpha over.
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
    MaskMultiply = 6,
    InvertAlpha = 7,
    InnerShadowCompose = 8,
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

    // GPU frame timing plumbing. When the caller opts in, executeDrawList
    // records the GPUStartTime / GPUEndTime of the frame's command buffer
    // and stashes the delta in `lastFrameGpuTimeNs` for
    // lastGpuFrameTimeNs() to surface. `frameTimingActive` gates a fresh
    // measurement window opened by beginGpuFrameTiming(); it resets to
    // false when the value is read to match Vulkan semantics.
    bool gpuTimingEnabled = false;
    bool gpuTimingActive = false;
    bool gpuTimingResultAvailable = false;
    std::uint64_t lastFrameGpuTimeNs = 0;

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
    case MetalPipelineKind::MaskMultiply:
        vsName = @"blur_vs";
        fsName = @"mask_multiply_fs";
        vd = ctx.blurVertexDesc;
        break;
    case MetalPipelineKind::InvertAlpha:
        vsName = @"blur_vs";
        fsName = @"invert_alpha_fs";
        vd = ctx.blurVertexDesc;
        break;
    case MetalPipelineKind::InnerShadowCompose:
        vsName = @"blur_vs";
        fsName = @"inner_shadow_compose_fs";
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
                                                                        bool generateMipmaps) const
{
    if (!context_ || !context_->deviceReady || width <= 0 || height <= 0 || pixels == nullptr) {
        return {};
    }
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
    if (!generateMipmaps) {
        std::vector<unsigned char> owned(source, source + static_cast<std::size_t>(width) * height * 4u);
        return createImageResourceRGBA(width, height, owned);
    }

    MTLTextureDescriptor *desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:YES];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = preferredTextureStorageMode(context_->device);
    id<MTLTexture> texture = [context_->device newTextureWithDescriptor:desc];
    if (texture == nil) {
        return {};
    }
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [texture replaceRegion:region mipmapLevel:0 withBytes:source
               bytesPerRow:static_cast<NSUInteger>(width) * 4];
    // Mip levels above 0 are populated by the GPU via a blit encoder.
    @autoreleasepool {
        id<MTLCommandBuffer> cb = [context_->commandQueue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit generateMipmapsForTexture:texture];
        [blit endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
    }
    context_->imageTextureCount += 1;
    return std::make_shared<MetalTextureResource>(texture, width, height, /*alpha=*/false, /*owned=*/true);
}

bool MetalRenderDevice::updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width,
                                                int height, const unsigned char *pixels,
                                                bool regenerateMipmaps) const
{
    auto *res = dynamic_cast<MetalTextureResource *>(imageResource.get());
    if (res == nullptr || !res->isValid() || pixels == nullptr) {
        return false;
    }
    if (!res->updateRGBA(x, y, width, height, pixels, false)) {
        return false;
    }
    if (regenerateMipmaps && res->metalTexture().mipmapLevelCount > 1) {
        @autoreleasepool {
            id<MTLCommandBuffer> cb = [context_->commandQueue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit generateMipmapsForTexture:res->metalTexture()];
            [blit endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
        }
    }
    return true;
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
// emit a ClipFill primitive.
//
// For a single clip, we rasterise straight into the mask target. Multiple
// clips must intersect (Canvas semantics), so each additional path is
// rasterised into a scratch target and then composed with the running
// accumulator through a MaskMultiply full-target pass. Plain repeated
// rasterisation with SrcOver / Multiply blends unions the shapes because
// pixels outside a triangle never invoke a fragment.
namespace {

// Fill an RGBA8 mask target with the tessellated triangles from a single
// clip resource, using the Solid pipeline.
void rasterizeSingleClipIntoTarget(id<MTLCommandBuffer> cb,
                                   MetalRenderDevice::MetalContext &ctx,
                                   id<MTLTexture> dst,
                                   const MetalClipMaskResource &mres,
                                   int canvasWidth, int canvasHeight)
{
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = dst;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
    [enc setViewport:makeViewport(canvasWidth, canvasHeight)];

    id<MTLRenderPipelineState> pso = obtainPipeline(ctx, MetalPipelineKind::Solid, 0);
    if (pso == nil) {
        [enc endEncoding];
        return;
    }
    [enc setRenderPipelineState:pso];
    MTLScissorRect rect;
    rect.x = 0; rect.y = 0;
    rect.width = static_cast<NSUInteger>(canvasWidth);
    rect.height = static_cast<NSUInteger>(canvasHeight);
    [enc setScissorRect:rect];

    const std::vector<float> &points = mres.points();
    const std::vector<float> &coverage = mres.coverage();
    const glm::mat4 &tf = mres.transform();
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
        [enc endEncoding];
        return;
    }
    const std::size_t vertexBytes = verts.size() * sizeof(wsc::CompactSolidVertex);
    if (vertexBytes <= 4096) {
        [enc setVertexBytes:verts.data() length:vertexBytes atIndex:0];
    } else {
        id<MTLBuffer> vb = [ctx.device newBufferWithBytes:verts.data()
                                                   length:vertexBytes
                                                  options:MTLResourceStorageModeShared];
        [enc setVertexBuffer:vb offset:0 atIndex:0];
    }
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:verts.size()];
    [enc endEncoding];
}

// Full-target compose: dst = tex0.r * tex1.r everywhere. Runs the
// mask_multiply pipeline via the standard blur vertex descriptor.
void composeMultiplyIntoTarget(id<MTLCommandBuffer> cb,
                               MetalRenderDevice::MetalContext &ctx,
                               id<MTLTexture> dst,
                               id<MTLTexture> texA,
                               id<MTLTexture> texB,
                               int canvasWidth, int canvasHeight)
{
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = dst;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

    id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
    [enc setViewport:makeViewport(canvasWidth, canvasHeight)];

    id<MTLRenderPipelineState> pso = obtainPipeline(ctx, MetalPipelineKind::MaskMultiply, /*blend=*/1 /*Src*/);
    if (pso == nil) {
        [enc endEncoding];
        return;
    }
    [enc setRenderPipelineState:pso];

    const float verts[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };
    [enc setVertexBytes:verts length:sizeof(verts) atIndex:0];
    [enc setFragmentTexture:texA atIndex:0];
    [enc setFragmentTexture:texB atIndex:1];
    [enc setFragmentSamplerState:ctx.clipSampler atIndex:0];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
    [enc endEncoding];
}

id<MTLTexture> allocateMaskTexture(id<MTLDevice> device, int canvasWidth, int canvasHeight)
{
    MTLTextureDescriptor *desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:static_cast<NSUInteger>(canvasWidth)
                                                          height:static_cast<NSUInteger>(canvasHeight)
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = preferredTextureStorageMode(device);
    return [device newTextureWithDescriptor:desc];
}

} // namespace

SharedImageResource MetalRenderDevice::rasterizeClipMask(const ClipMaskState &state,
                                                         int canvasWidth, int canvasHeight) const
{
    if (!context_ || !context_->deviceReady || state.resources.empty()
        || canvasWidth <= 0 || canvasHeight <= 0) {
        return {};
    }

    id<MTLTexture> finalTex = nil;
    @autoreleasepool {
        id<MTLTexture> accum = allocateMaskTexture(context_->device, canvasWidth, canvasHeight);
        id<MTLTexture> scratch = nil;
        id<MTLTexture> composed = nil;
        if (accum == nil) {
            return {};
        }

        id<MTLCommandBuffer> cb = [context_->commandQueue commandBuffer];

        bool haveAccum = false;
        for (const SharedClipMaskResource &resource : state.resources) {
            const auto *mres = dynamic_cast<const MetalClipMaskResource *>(resource.get());
            if (mres == nullptr || !mres->isValid()) {
                continue;
            }
            if (!haveAccum) {
                rasterizeSingleClipIntoTarget(cb, *context_, accum, *mres,
                                              canvasWidth, canvasHeight);
                haveAccum = true;
                continue;
            }
            // Lazy-allocate scratch + composed once we know there is more than
            // one active clip. Both are reused across subsequent iterations.
            if (scratch == nil) {
                scratch = allocateMaskTexture(context_->device, canvasWidth, canvasHeight);
                composed = allocateMaskTexture(context_->device, canvasWidth, canvasHeight);
                if (scratch == nil || composed == nil) {
                    break;
                }
            }
            rasterizeSingleClipIntoTarget(cb, *context_, scratch, *mres,
                                          canvasWidth, canvasHeight);
            composeMultiplyIntoTarget(cb, *context_, composed, accum, scratch,
                                      canvasWidth, canvasHeight);
            std::swap(accum, composed);
        }

        [cb commit];
        [cb waitUntilCompleted];

        finalTex = haveAccum ? accum : nil;
    }

    if (finalTex == nil) {
        return {};
    }
    context_->imageTextureCount += 1;
    return std::make_shared<MetalTextureResource>(finalTex, canvasWidth, canvasHeight,
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
    float params[4];             // x = layerAlpha, y = roundedRadius, zw = quad size (px)
    float useColorMatrix[4];     // x flag (1.0 = active), yzw padding
    float colorMatrixRow0[4];
    float colorMatrixRow1[4];
    float colorMatrixRow2[4];
    float colorMatrixRow3[4];
    float colorMatrixOffset[4];
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
    // The rounded-rectangle uniform slot is repurposed to carry (radius, w, h)
    // in destination-pixel units. The Textured fragment shader activates the
    // rounded-corner branch only when both radius > 0.5 and size > 0.
    u.params[1] = prim.roundedRadius;
    u.params[2] = prim.roundedWidth;
    u.params[3] = prim.roundedHeight;
    u.useColorMatrix[0] = prim.hasColorMatrix ? 1.0f : 0.0f;
    if (prim.hasColorMatrix) {
        for (int row = 0; row < 4; ++row) {
            float *dst = (row == 0 ? u.colorMatrixRow0
                        : row == 1 ? u.colorMatrixRow1
                        : row == 2 ? u.colorMatrixRow2
                                   : u.colorMatrixRow3);
            for (int col = 0; col < 4; ++col) {
                dst[col] = prim.colorMatrix[row * 4 + col];
            }
        }
        u.colorMatrixOffset[0] = prim.colorMatrixOffset[0];
        u.colorMatrixOffset[1] = prim.colorMatrixOffset[1];
        u.colorMatrixOffset[2] = prim.colorMatrixOffset[2];
        u.colorMatrixOffset[3] = prim.colorMatrixOffset[3];
    }
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
        if (context_->gpuTimingActive) {
            const CFTimeInterval start = cb.GPUStartTime;
            const CFTimeInterval end = cb.GPUEndTime;
            if (end > start) {
                context_->lastFrameGpuTimeNs =
                    static_cast<std::uint64_t>((end - start) * 1'000'000'000.0);
                context_->gpuTimingResultAvailable = true;
            }
            context_->gpuTimingActive = false;
        }
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
    float applyPost = 0.0f;   // > 0.5 activates saturation/brightness/contrast/grain
    float saturation = 1.0f;
    float colorAdjust[4] = {1.0f, 1.0f, 0.0f, 0.0f}; // brightness, contrast, grain, unused
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
    auto *srcResource = dynamic_cast<const MetalTextureResource *>(source.get());
    if (srcResource == nullptr || !srcResource->isValid()) {
        return {};
    }
    if (!filter.isValid()) {
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

    if (filter.type() == wsc::ImageFilter::Type::Blur) {
        // Fold post-blur colour adjustments and grain into the vertical
        // (final) pass so they only take effect once. Skia's blur-with-color
        // model keeps colour untouched during accumulation and applies the
        // transforms once at the end.
        if (filter.hasColorAdjustment() || filter.hasGrain()) {
            uY.applyPost = 1.0f;
            uY.saturation = filter.saturation();
            uY.colorAdjust[0] = filter.brightness();
            uY.colorAdjust[1] = filter.contrast();
            uY.colorAdjust[2] = filter.grain();
        }
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

    if (filter.type() == wsc::ImageFilter::Type::InnerShadow) {
        // Provision four full-target textures: one for the inverted alpha,
        // two ping-pong buffers for the horizontal + vertical blur, and one
        // final composite. Reuse the same allocator as the Blur path.
        id<MTLTexture> inverted = nil;
        id<MTLTexture> blurH = nil;
        id<MTLTexture> blurV = nil;
        id<MTLTexture> finalTex = nil;
        @autoreleasepool {
            MTLTextureUsage usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            inverted = makeFilterTexture(context_->device, width, height, usage);
            blurH = makeFilterTexture(context_->device, width, height, usage);
            blurV = makeFilterTexture(context_->device, width, height, usage);
            finalTex = makeFilterTexture(context_->device, width, height, usage);
            if (!inverted || !blurH || !blurV || !finalTex) {
                return {};
            }

            id<MTLCommandBuffer> cb = [context_->commandQueue commandBuffer];
            id<MTLTexture> src = srcResource->metalTexture();

            // Pass 1: invert alpha into `inverted`.
            {
                MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = inverted;
                pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
                id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
                [enc setViewport:makeViewport(width, height)];
                id<MTLRenderPipelineState> pso = obtainPipeline(*context_, MetalPipelineKind::InvertAlpha, /*Src=*/1);
                if (pso != nil) {
                    [enc setRenderPipelineState:pso];
                    const float verts[] = {
                        -1.0f, -1.0f, 0.0f, 0.0f,  1.0f, -1.0f, 1.0f, 0.0f,  1.0f,  1.0f, 1.0f, 1.0f,
                        -1.0f, -1.0f, 0.0f, 0.0f,  1.0f,  1.0f, 1.0f, 1.0f, -1.0f,  1.0f, 0.0f, 1.0f,
                    };
                    [enc setVertexBytes:verts length:sizeof(verts) atIndex:0];
                    [enc setFragmentTexture:src atIndex:0];
                    [enc setFragmentSamplerState:context_->clipSampler atIndex:0];
                    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
                }
                [enc endEncoding];
            }
            // Passes 2+3: separable Gaussian blur on inverted alpha.
            encodeBlurPass(cb, *context_, inverted, blurH, uX, width, height);
            encodeBlurPass(cb, *context_, blurH, blurV, uY, width, height);

            // Pass 4: composite source + blurred inverted alpha into `finalTex`.
            {
                MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = finalTex;
                pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
                id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
                [enc setViewport:makeViewport(width, height)];
                id<MTLRenderPipelineState> pso = obtainPipeline(*context_, MetalPipelineKind::InnerShadowCompose, /*Src=*/1);
                if (pso != nil) {
                    [enc setRenderPipelineState:pso];
                    const float verts[] = {
                        -1.0f, -1.0f, 0.0f, 0.0f,  1.0f, -1.0f, 1.0f, 0.0f,  1.0f,  1.0f, 1.0f, 1.0f,
                        -1.0f, -1.0f, 0.0f, 0.0f,  1.0f,  1.0f, 1.0f, 1.0f, -1.0f,  1.0f, 0.0f, 1.0f,
                    };
                    [enc setVertexBytes:verts length:sizeof(verts) atIndex:0];

                    struct ComposeUniforms {
                        float color[4];
                        float offsetUv[2];
                        float padding[2];
                    } u{};
                    const wsc::Color c = filter.shadowColor();
                    u.color[0] = static_cast<float>(c.getR()) / 255.0f;
                    u.color[1] = static_cast<float>(c.getG()) / 255.0f;
                    u.color[2] = static_cast<float>(c.getB()) / 255.0f;
                    u.color[3] = static_cast<float>(c.getA()) / 255.0f;
                    u.offsetUv[0] = filter.offsetX() / static_cast<float>(width);
                    u.offsetUv[1] = filter.offsetY() / static_cast<float>(height);
                    [enc setFragmentBytes:&u length:sizeof(u) atIndex:0];

                    [enc setFragmentTexture:src atIndex:0];
                    [enc setFragmentTexture:blurV atIndex:1];
                    [enc setFragmentSamplerState:context_->clipSampler atIndex:0];
                    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
                }
                [enc endEncoding];
            }

            [cb commit];
            [cb waitUntilCompleted];
        }

        if (finalTex == nil) {
            return {};
        }
        if (executionStats != nullptr) {
            executionStats->passCount = 4;
            executionStats->pixelPassCount = 4;
            executionStats->downsampled = false;
        }
        context_->imageTextureCount += 1;
        return std::make_shared<MetalTextureResource>(finalTex, width, height,
                                                      /*alpha=*/false, /*owned=*/true);
    }

    return {};
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

// -----------------------------------------------------------------------------
// GPU frame timing. Metal exposes GPUStartTime / GPUEndTime on every
// MTLCommandBuffer after waitUntilCompleted, so we don't need
// MTLCounterSampleBuffer for the coarse frame-level measurement Canvas asks
// for. beginGpuFrameTiming arms the next frame; executeDrawList captures the
// delta and stores it for lastGpuFrameTimeNs. Behaves as an inert stub when
// the caller has not enabled timing.
bool MetalRenderDevice::beginGpuFrameTiming()
{
    if (!context_ || !context_->deviceReady || !context_->gpuTimingEnabled) {
        return false;
    }
    context_->gpuTimingActive = true;
    context_->gpuTimingResultAvailable = false;
    return true;
}

void MetalRenderDevice::endGpuFrameTiming()
{
    // executeDrawList closes out the window on its own once the command
    // buffer completes; nothing to do here besides matching the interface.
}

void MetalRenderDevice::setGpuFrameTimingEnabled(bool enabled)
{
    if (!context_) {
        return;
    }
    context_->gpuTimingEnabled = enabled;
    if (!enabled) {
        context_->gpuTimingActive = false;
        context_->gpuTimingResultAvailable = false;
    }
}

bool MetalRenderDevice::lastGpuFrameTimeNs(std::uint64_t &nanoseconds) const
{
    if (!context_ || !context_->gpuTimingResultAvailable) {
        nanoseconds = 0;
        return false;
    }
    nanoseconds = context_->lastFrameGpuTimeNs;
    return true;
}

// -----------------------------------------------------------------------------
// On-screen presentation via CAMetalLayer.
//
// The swapchain does not own the render loop: MetalRenderDevice keeps its
// existing offscreen render target (`lastReadbackTexture`) as the canonical
// frame source, and present() blit-copies that texture into the CAMetalLayer's
// nextDrawable. That mirrors the Vulkan pattern (VulkanSwapchain::present
// reuses the readback image) and avoids threading swapchain drawables
// through the Renderer's frame execution.
//
// NativeSurface::Cocoa is expected to carry either an NSView* (in which case
// we install a CAMetalLayer as its backing layer) or an existing
// CAMetalLayer* (used as-is). Populating an NSWindow* directly is not
// supported — call sites typically resolve to the window's contentView.
namespace {

CAMetalLayer *coerceLayerFromSurface(const NativeSurface &surface, id<MTLDevice> device,
                                     CGSize drawableSize)
{
    if (surface.platform != NativeSurface::Platform::Cocoa || surface.window == nullptr) {
        return nil;
    }
    id obj = (__bridge id)surface.window;
    CAMetalLayer *layer = nil;
    if ([obj isKindOfClass:[CAMetalLayer class]]) {
        layer = (CAMetalLayer *)obj;
    }
#if TARGET_OS_OSX
    else if ([obj isKindOfClass:[NSView class]]) {
        NSView *view = (NSView *)obj;
        view.wantsLayer = YES;
        if ([view.layer isKindOfClass:[CAMetalLayer class]]) {
            layer = (CAMetalLayer *)view.layer;
        } else {
            layer = [CAMetalLayer layer];
            view.layer = layer;
        }
    }
#elif TARGET_OS_IPHONE || TARGET_OS_TV
    else if ([obj isKindOfClass:[UIView class]]) {
        UIView *view = (UIView *)obj;
        if ([view.layer isKindOfClass:[CAMetalLayer class]]) {
            layer = (CAMetalLayer *)view.layer;
        }
    }
#endif
    if (layer == nil) {
        return nil;
    }
    if (layer.device == nil) {
        layer.device = device;
    }
    layer.pixelFormat = MTLPixelFormatRGBA8Unorm;
    layer.framebufferOnly = NO;
    if (drawableSize.width > 0.0 && drawableSize.height > 0.0) {
        layer.drawableSize = drawableSize;
    }
    return layer;
}

class MetalSwapchain final : public ISwapchain
{
public:
    MetalSwapchain(MetalRenderDevice *owner, CAMetalLayer *layer,
                   const SwapchainConfig &config)
        : owner_(owner), layer_(layer), config_(config)
    {
        if (@available(macOS 10.13, iOS 8.0, tvOS 9.0, *)) {
            layer_.displaySyncEnabled = config_.vsync ? YES : NO;
        }
    }

    AcquiredImage acquire() override
    {
        AcquiredImage img;
        if (layer_ == nil) {
            return img;
        }
        img.width = static_cast<int>(layer_.drawableSize.width);
        img.height = static_cast<int>(layer_.drawableSize.height);
        img.valid = true;
        return img;
    }

    bool present() override
    {
        if (owner_ == nullptr || layer_ == nil) {
            return false;
        }
        id<MTLTexture> src = (__bridge id<MTLTexture>)(void *)owner_->nativeHandle(2);
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)(void *)owner_->nativeHandle(1);
        if (src == nil || queue == nil) {
            return false;
        }
        @autoreleasepool {
            id<CAMetalDrawable> drawable = [layer_ nextDrawable];
            if (drawable == nil) {
                return false;
            }
            id<MTLTexture> dst = drawable.texture;
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            MTLSize copySize = MTLSizeMake(std::min<NSUInteger>(src.width, dst.width),
                                           std::min<NSUInteger>(src.height, dst.height), 1);
            [blit copyFromTexture:src
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:copySize
                        toTexture:dst
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
            [cb presentDrawable:drawable];
            [cb commit];
        }
        return true;
    }

    void resize(int width, int height) override
    {
        if (layer_ != nil && width > 0 && height > 0) {
            layer_.drawableSize = CGSizeMake(width, height);
        }
    }

private:
    MetalRenderDevice *owner_ = nullptr;
    CAMetalLayer *layer_ = nil;
    SwapchainConfig config_{};
};

} // namespace

bool MetalRenderDevice::supportsPresentation() const
{
    return context_ && context_->deviceReady;
}

std::unique_ptr<ISwapchain> MetalRenderDevice::createSwapchain(const NativeSurface &surface,
                                                               const SwapchainConfig &config)
{
    if (!context_ || !context_->deviceReady) {
        return nullptr;
    }
    CGSize drawableSize = CGSizeMake(0.0, 0.0);
    CAMetalLayer *layer = coerceLayerFromSurface(surface, context_->device, drawableSize);
    if (layer == nil) {
        return nullptr;
    }
    return std::make_unique<MetalSwapchain>(this, layer, config);
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
