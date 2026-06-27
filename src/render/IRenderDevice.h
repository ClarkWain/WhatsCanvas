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
};
