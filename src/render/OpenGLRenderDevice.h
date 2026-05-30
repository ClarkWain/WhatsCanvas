#pragma once

#include "IRenderDevice.h"

class OpenGLRenderDevice : public IRenderDevice
{
public:
    OpenGLRenderDevice() = default;
    ~OpenGLRenderDevice() override;

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
    SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                      const OffscreenRenderRequest &request) const override;

private:
    bool backendInitialized_ = false;
};