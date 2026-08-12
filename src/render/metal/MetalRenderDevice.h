#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../DrawList.h"
#include "../IRenderDevice.h"

class RenderTargetPool;

/// Metal implementation of the WhatsCanvas render-device abstraction.
///
/// Mirrors the Vulkan backend's compilation model: the class is always
/// referenceable by the render-device factory, but the real Metal API path
/// is only compiled in when the project is configured with
/// `WHATSCANVAS_ENABLE_METAL` on an Apple platform (macOS / iOS / tvOS). On
/// non-Apple targets or when the option is off, the implementation lowers to
/// an inert stub whose methods return failure so callers gracefully fall back
/// to another backend.
///
/// The current implementation covers:
///   * offscreen render targets backed by `id<MTLTexture>`
///   * backend-neutral `wsc::DrawList` execution for the SolidTriangles,
///     TexturedQuad, GradientFill, and ClipFill primitives
///   * `executeCommands()` translation via `CommandDrawListEncoder`
///   * image resources (RGBA8 + Alpha8) with sub-region updates
///   * external image wrapping (id<MTLTexture> handle round-trip)
///   * synchronous readback via a shared/managed MTLBuffer
///
/// Presentation, image filters, and mipmap generation are follow-ups.
class MetalRenderDevice : public IRenderDevice
{
public:
    MetalRenderDevice();
    ~MetalRenderDevice() override;

    MetalRenderDevice(const MetalRenderDevice &) = delete;
    MetalRenderDevice &operator=(const MetalRenderDevice &) = delete;

    /// Returns true when Metal support was compiled into this build. This does
    /// not guarantee a compatible GPU is present at runtime; that is only known
    /// after a successful initializeBackend().
    static bool isAvailable();

    void initializeBackend() override;
    void finalizeBackend() override;
    bool readPixelsRGBA(int width, int height, std::vector<unsigned char> &pixels) const override;
    std::unique_ptr<IRenderTarget> createRenderTarget(int width, int height) const override;
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const override;
    SharedImageResource createImageResourceRGBA(int width, int height,
                                                const std::vector<unsigned char> &pixels) const override;
    SharedImageResource createImageResourceAlpha8(
        int width, int height,
        const std::vector<unsigned char> &pixels) const override;
    SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                         const unsigned char *pixels,
                                                         bool generateMipmaps) const override;
    bool updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                 const unsigned char *pixels, bool regenerateMipmaps) const override;
    bool updateImageResourceAlpha8(
        const SharedImageResource &imageResource,
        int x, int y, int width, int height,
        const unsigned char *pixels) const override;
    SharedImageResource wrapExternalImageResource(ImageResourceHandle handle) const override;

    /// Metal-specific: return the underlying id<MTLTexture> pointer of an
    /// owned texture resource as an ImageResourceHandle (64-bit), suitable for
    /// round-tripping through wrapExternalImageResource. Returns an invalid
    /// handle for non-Metal resources.
    ImageResourceHandle nativeImageHandle(const SharedImageResource &resource) const;

    RenderResourceStats resourceStats() const override;
    SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                      const OffscreenRenderRequest &request) const override;

    /// Gaussian blur via a separable two-pass shader (horizontal then
    /// vertical). Ignores color-adjust / grain / inner-shadow modifiers on the
    /// filter (Stage 3 follow-up); returns an empty resource for non-Blur
    /// filter types so the Canvas image-filter chain falls back gracefully.
    SharedImageResource filterImageResource(const SharedImageResource &source,
                                            int width, int height,
                                            const wsc::ImageFilter &filter,
                                            FilterExecutionStats *executionStats = nullptr) const override;

    /// Backend-neutral entry point: execute a wsc::DrawList into an offscreen
    /// render target. Public so tests can drive the low-level path directly.
    bool executeDrawList(const std::unique_ptr<IRenderTarget> &target, const wsc::DrawList &drawList) const;

    bool executeCommands(const std::unique_ptr<IRenderTarget> &target,
                         const std::vector<std::unique_ptr<Command>> &commands,
                         const OffscreenRenderRequest &request) const override;

    std::size_t lastExecutionDrawCallCount() const override { return lastExecutionDrawCallCount_; }
    std::size_t lastExecutionMergedBatchCount() const override { return lastExecutionMergedBatchCount_; }
    std::size_t lastCompiledPacketCount() const override { return lastCompiledPacketCount_; }
    std::size_t lastCompiledVertexBytes() const override { return lastCompiledVertexBytes_; }
    std::size_t lastCompiledIndexBytes() const override { return lastCompiledIndexBytes_; }
    std::uint64_t lastFrameCompileCpuTimeNs() const override { return lastFrameCompileCpuTimeNs_; }

    /// Metal drives its main-target frame through executeCommands(), the same
    /// path Vulkan uses. Renderer will flush the frame via executeCommands()
    /// and read the result with readPixelsRGBA().
    bool usesDeviceCommandExecution() const override { return true; }

    /// Metal supports on-screen presentation on Apple platforms via
    /// CAMetalLayer swapchains. Non-Apple / Metal-disabled builds return false.
    bool supportsPresentation() const override;
    std::unique_ptr<ISwapchain> createSwapchain(const NativeSurface &surface,
                                                const SwapchainConfig &config) override;

    bool beginGpuFrameTiming() override;
    void endGpuFrameTiming() override;
    void setGpuFrameTimingEnabled(bool enabled) override;
    bool lastGpuFrameTimeNs(std::uint64_t &nanoseconds) const override;

    /// True once the Metal device has been created successfully.
    bool isDeviceReady() const;

    /// Human-readable name of the selected physical device (Metal GPU).
    const std::string &selectedDeviceName() const;

    /// Raw native handle accessor for advanced interop. `which` selects:
    ///   0: id<MTLDevice> (the default system device)
    ///   1: id<MTLCommandQueue>
    ///   2: last render-target texture (id<MTLTexture>) — the one Canvas
    ///      most recently rendered into, suitable for blit-presenting to a
    ///      CAMetalLayer drawable.
    /// Returns 0 when Metal is not compiled in or the device is not ready.
    std::uintptr_t nativeHandle(int which) const override;

    /// Opaque backend context, defined in the .mm translation unit.
    struct MetalContext;

private:
    /// Rasterize a Canvas clip-mask state (a set of coverage-annotated path
    /// resources produced by `createClipMaskResource`) into an RGBA8 mask
    /// texture whose red channel carries the aggregate coverage. Returns an
    /// invalid image when the state is empty or the device is not ready.
    /// The ClipFill fragment shader samples the red channel, so this format
    /// is the natural drop-in for the encoder's `createClipMaskTexture` hook.
    SharedImageResource rasterizeClipMask(const ClipMaskState &state,
                                          int canvasWidth, int canvasHeight) const;

    std::unique_ptr<MetalContext> context_;
    mutable std::size_t lastExecutionDrawCallCount_ = 0;
    mutable std::size_t lastExecutionMergedBatchCount_ = 0;
    mutable std::size_t lastCompiledPacketCount_ = 0;
    mutable std::size_t lastCompiledVertexBytes_ = 0;
    mutable std::size_t lastCompiledIndexBytes_ = 0;
    mutable std::uint64_t lastFrameCompileCpuTimeNs_ = 0;
    bool backendInitialized_ = false;
};
