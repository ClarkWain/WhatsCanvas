#pragma once

#include <memory>
#include <vector>

#include "command/DrawCommand.h"

struct OffscreenRenderRequest
{
    int canvasWidth = 0;
    int canvasHeight = 0;
    int targetWidth = 0;
    int targetHeight = 0;
    int viewportX = 0;
    int viewportY = 0;
    int scissorOffsetX = 0;
    int scissorOffsetY = 0;
};

class IRenderer
{
public:
    virtual ~IRenderer() = default;

    virtual void initializeBackend() = 0;
    virtual void finalizeBackend() = 0;
    virtual void setViewport(int width, int height) = 0;
    virtual void submit(std::unique_ptr<Command> &&command) = 0;
    virtual size_t commandCount() const = 0;
    virtual std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t index) = 0;
    virtual void appendCommands(std::vector<std::unique_ptr<Command>> &&commands) = 0;
    virtual bool readPixelsRGBA(std::vector<unsigned char> &pixels) const = 0;
    virtual SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const = 0;
    virtual SharedImageResource createImageResourceRGBA(int width, int height, const std::vector<unsigned char> &pixels) const = 0;
    virtual SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                                 const unsigned char *pixels, bool generateMipmaps) const = 0;
    virtual SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                              const OffscreenRenderRequest &request) const = 0;
    virtual void resetRenderState() = 0;
    virtual void clear() = 0;
    virtual void flush() = 0;
};