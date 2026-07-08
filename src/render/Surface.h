#pragma once

#include <memory>

#include "wsc/Surface.h"

// Backend-neutral presentation abstraction (design: windowed-presentation).
//
// The public window/config types (NativeSurface, SwapchainConfig) live in
// include/wsc/Surface.h; the aliases below let existing internal code reference
// them unqualified. The swapchain interface itself is internal.

using NativeSurface = wsc::NativeSurface;
using SwapchainConfig = wsc::SwapchainConfig;
using OutputTarget = wsc::OutputTarget;

/// Internal device-level description of a host-owned backend render target
/// (wrap-external). The public API expresses this via wsc::OutputTarget; Canvas
/// converts to this type before calling the render backend.
struct BackendRenderTarget
{
	enum class Kind
	{
		None,
		OpenGLFramebuffer, ///< `glFramebuffer` is a GL FBO name (0 = default).
		VulkanImage,       ///< `nativeHandle` is a VkImage; `nativeFormat` a VkFormat.
		MetalTexture,      ///< `nativeHandle` is an id<MTLTexture>.
		D3DTexture,        ///< `nativeHandle` is an ID3D11Texture2D* / D3D12 resource.
	};

	Kind kind = Kind::None;
	unsigned int glFramebuffer = 0;      ///< OpenGLFramebuffer.
	void *nativeHandle = nullptr;        ///< VkImage / id<MTLTexture> / D3D texture.
	unsigned long long nativeFormat = 0; ///< Backend format enum (e.g. VkFormat).
	int width = 0;
	int height = 0;
};

/// The render target for the current frame, as handed back by a swapchain. The
/// handle is backend-specific (e.g. a VkImage, a GL framebuffer id, or an
/// id<MTLTexture>); `valid` is false when acquisition failed (e.g. the surface
/// is out of date and needs a resize/recreate).
struct AcquiredImage
{
    void *handle = nullptr;
    int width = 0;
    int height = 0;
    bool valid = false;
};

/// Backend-neutral on-screen presentation target. Each backend provides a
/// concrete implementation (Vulkan swapchain, GL host-owned shell, Metal
/// CAMetalLayer, software blit). Owned by the layer that drives the frame.
class ISwapchain
{
public:
    virtual ~ISwapchain() = default;

    /// Acquire this frame's render target. May return an invalid image when the
    /// surface is out of date; the caller should then resize() and retry.
    virtual AcquiredImage acquire() = 0;

    /// Present the most recently rendered frame to the window. Returns false
    /// when the surface became out of date (caller should resize()).
    virtual bool present() = 0;

    /// Rebuild for a new drawable size (window resize, or surface out of date).
    virtual void resize(int width, int height) = 0;
};
