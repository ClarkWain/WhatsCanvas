#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../IRenderDevice.h"

/// Vulkan implementation of the WhatsCanvas render-device abstraction.
///
/// This backend is built unconditionally so that the render-device factory can
/// always reference it, but the actual Vulkan API usage is only compiled when
/// the project is configured with `WHATSCANVAS_ENABLE_VULKAN` (which requires
/// the Vulkan SDK to be available at configure time).
///
/// The current milestone brings the device up to a usable logical-device state
/// (instance creation, physical-device selection, queue-family discovery and
/// logical-device creation). The command-execution and resource-creation
/// entry points are intentional stubs that will be filled in by later
/// milestones once the pipeline and memory management layers land.
class VulkanRenderDevice : public IRenderDevice
{
public:
    VulkanRenderDevice();
    ~VulkanRenderDevice() override;

    VulkanRenderDevice(const VulkanRenderDevice &) = delete;
    VulkanRenderDevice &operator=(const VulkanRenderDevice &) = delete;

    /// Returns true when Vulkan support was compiled into this build.
    /// This does not guarantee a compatible GPU is present at runtime; that is
    /// only known after a successful initializeBackend().
    static bool isAvailable();

    void initializeBackend() override;
    void finalizeBackend() override;
    bool readPixelsRGBA(int width, int height, std::vector<unsigned char> &pixels) const override;
    std::unique_ptr<IRenderTarget> createRenderTarget(int width, int height) const override;
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const override;
    SharedImageResource createImageResourceRGBA(int width, int height,
                                                const std::vector<unsigned char> &pixels) const override;
    SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                         const unsigned char *pixels,
                                                         bool generateMipmaps) const override;
    bool updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                 const unsigned char *pixels, bool regenerateMipmaps) const override;
    SharedImageResource wrapExternalImageResource(ImageResourceHandle handle) const override;
    RenderResourceStats resourceStats() const override;
    SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                      const OffscreenRenderRequest &request) const override;

    /// True once a Vulkan logical device has been created successfully.
    bool isDeviceReady() const;

    /// Human-readable name of the selected physical device, or an empty string
    /// when no device has been selected.
    const std::string &selectedDeviceName() const;

    /// Vulkan-specific capability: fill an offscreen render target with a solid
    /// RGBA color using a device clear, leaving it ready for readPixelsRGBA().
    /// Returns false when Vulkan is not compiled in, the device is not ready, or
    /// the target is not a Vulkan render target. Used by validation and as a
    /// building block until the draw-command pipeline lands.
    bool fillRenderTargetSolid(const std::unique_ptr<IRenderTarget> &target, unsigned char r, unsigned char g,
                               unsigned char b, unsigned char a) const;

    /// Vulkan-specific M3 capability: rasterize a solid-colored triangle list
    /// into an offscreen render target through a real graphics pipeline, then
    /// leave it ready for readPixelsRGBA(). `ndcPositions` holds interleaved
    /// x,y pairs in Vulkan normalized device coordinates (3 vertices per
    /// triangle). Returns false when Vulkan is unavailable, the device is not
    /// ready, the target is not a Vulkan render target, or the input is invalid.
    bool renderSolidTriangles(const std::unique_ptr<IRenderTarget> &target, const std::vector<float> &ndcPositions,
                              float r, float g, float b, float a) const;

    /// Primitive topology for renderSolidPrimitives().
    enum class SolidTopology
    {
        Triangles, ///< Triangle list (3 vertices per triangle).
        Lines,     ///< Line list (2 vertices per line).
        Points     ///< Point list (1 vertex per point).
    };

    /// Vulkan-specific M3 capability: rasterize solid-colored primitives of the
    /// given topology into an offscreen render target. `ndcPositions` holds
    /// interleaved x,y pairs in Vulkan NDC. Returns false on invalid input or
    /// when Vulkan is unavailable.
    bool renderSolidPrimitives(const std::unique_ptr<IRenderTarget> &target, SolidTopology topology,
                               const std::vector<float> &ndcPositions, float r, float g, float b, float a) const;

    /// Blend mode for renderBlendedOverlay(). Fixed-function approximations that
    /// mirror the OpenGL backend's supported modes.
    enum class SolidBlendMode
    {
        SrcOver,  ///< src.a, 1-src.a (default over).
        Src,      ///< replace destination.
        Add,      ///< additive.
        Multiply, ///< src * dst.
        Screen    ///< 1 - (1-src)(1-dst).
    };

    /// Vulkan-specific M4 capability: rasterize a triangle list with per-vertex
    /// colors (fragment-interpolated gradient). `rgbaPerVertex` holds 4 floats
    /// per vertex, matching the vertex count implied by `ndcPositions`.
    bool renderGradientTriangles(const std::unique_ptr<IRenderTarget> &target, const std::vector<float> &ndcPositions,
                                 const std::vector<float> &rgbaPerVertex) const;

    /// Vulkan-specific M4 capability: draw a full-target opaque background, then
    /// a full-target foreground with the given blend mode over it. Leaves the
    /// target ready for readPixelsRGBA(). Used to validate blend modes.
    bool renderBlendedOverlay(const std::unique_ptr<IRenderTarget> &target, SolidBlendMode blendMode,
                              float bgR, float bgG, float bgB, float bgA, float fgR, float fgG, float fgB,
                              float fgA) const;

    /// Vulkan-specific M5 capability: draw a full-target quad sampling the given
    /// image resource (created via createImageResourceRGBA/FromImageData) into
    /// an offscreen render target, then leave it ready for readPixelsRGBA().
    bool renderTexturedQuad(const std::unique_ptr<IRenderTarget> &target,
                            const SharedImageResource &imageResource) const;

    /// Vulkan-specific M6 capability: composite an already-rendered offscreen
    /// layer onto a destination target over a solid background, using a layer
    /// alpha. Demonstrates the saveLayer composite-back step. Note: the generic
    /// renderCommandsToImageResource() remains pending a backend-neutral command
    /// layer (see the Vulkan roadmap, M6 / section 3).
    bool compositeLayer(const std::unique_ptr<IRenderTarget> &dstTarget,
                        const std::unique_ptr<IRenderTarget> &layerTarget, float bgR, float bgG, float bgB, float bgA,
                        float layerAlpha) const;

    /// Opaque backend context, defined in the implementation file.
    struct VulkanContext;

private:
    std::unique_ptr<VulkanContext> context_;
    bool backendInitialized_ = false;
};
