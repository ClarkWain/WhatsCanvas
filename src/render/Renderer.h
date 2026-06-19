#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "IRenderDevice.h"
#include "IRenderer.h"
#include "RenderContext.h"

// Forward declaration for backend type enum.
enum class RenderBackendType;

class Renderer : public IRenderer
{
public:
    static void initialize();
    static void finalize();

public:
    Renderer();
    explicit Renderer(std::unique_ptr<IRenderDevice> device);
    ~Renderer() override;

    void initializeBackend() override;
    void finalizeBackend() override;

    void setViewport(int width, int height) override;
    void submit(std::unique_ptr<Command> &&command) override;
    size_t commandCount() const override;
    std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t index) override;
    void appendCommands(std::vector<std::unique_ptr<Command>> &&commands) override;

    bool readPixelsRGBA(std::vector<unsigned char> &pixels) const override;
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const override;
    SharedImageResource createImageResourceRGBA(int width, int height, const std::vector<unsigned char> &pixels) const override;
    SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                         const unsigned char *pixels, bool generateMipmaps) const override;
    SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                      const OffscreenRenderRequest &request) const override;
    void resetRenderState() override;
    void clear() override;
    void flush() override;

private:
    std::vector<std::unique_ptr<Command>> commands_;
    std::unique_ptr<IRenderDevice> device_;
    RenderContext context_;
    bool backendInitialized_ = false;
};
