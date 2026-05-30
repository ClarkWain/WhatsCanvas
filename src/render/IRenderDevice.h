#pragma once

#include <memory>
#include <vector>

class Command;
class ImageResource;
class IRenderTarget;
class ClipMaskResource;
struct OffscreenRenderRequest;
struct ClipMaskPath;

using SharedImageResource = std::shared_ptr<ImageResource>;
using SharedClipMaskResource = std::shared_ptr<ClipMaskResource>;

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
    virtual SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                              const OffscreenRenderRequest &request) const = 0;
};