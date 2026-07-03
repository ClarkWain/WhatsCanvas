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

    /// Opaque backend context, defined in the implementation file.
    struct VulkanContext;

private:
    std::unique_ptr<VulkanContext> context_;
    bool backendInitialized_ = false;
};
