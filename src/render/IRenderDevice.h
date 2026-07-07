#pragma once

#include <memory>
#include <vector>

#include "RenderTypes.h"

class Command;
class IRenderTarget;
struct OffscreenRenderRequest;
struct ClipMaskPath;

class IRenderDevice
{
public:
    virtual ~IRenderDevice() = default;

    virtual void initializeBackend() = 0;
    virtual void finalizeBackend() = 0;
    virtual bool readPixelsRGBA(int width, int height, std::vector<unsigned char> &pixels) const = 0;
    virtual std::unique_ptr<IRenderTarget> createRenderTarget(int width, int height) const = 0;
    virtual SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const = 0;
    virtual SharedImageResource createImageResourceRGBA(int width, int height,
                                                        const std::vector<unsigned char> &pixels) const = 0;
    virtual SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                                 const unsigned char *pixels,
                                                                 bool generateMipmaps) const = 0;
    virtual bool updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                         const unsigned char *pixels, bool regenerateMipmaps) const = 0;
    virtual SharedImageResource wrapExternalImageResource(ImageResourceHandle handle) const = 0;
    virtual RenderResourceStats resourceStats() const = 0;
    virtual SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                              const OffscreenRenderRequest &request) const = 0;

    /// Whether this device drives its main-target frame by rendering a recorded
    /// command stream through executeCommands() (e.g. Vulkan) rather than the
    /// OpenGL-style per-command execute() path. When true, Renderer flushes the
    /// frame via executeCommands() into a device render target and reads it back
    /// with readPixelsRGBA().
    virtual bool usesDeviceCommandExecution() const { return false; }

    /// Render a command stream into the given render target, leaving it ready
    /// for readPixelsRGBA(). Only meaningful when usesDeviceCommandExecution()
    /// is true; the default returns false (unsupported).
    virtual bool executeCommands(const std::unique_ptr<IRenderTarget> & /*target*/,
                                 const std::vector<std::unique_ptr<Command>> & /*commands*/,
                                 const OffscreenRenderRequest & /*request*/) const { return false; }
};
