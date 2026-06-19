#pragma once

#include "RenderTypes.h"

struct OffscreenRenderRequest;

class IRenderTarget
{
public:
    virtual ~IRenderTarget() = default;

    virtual bool isValid() const = 0;

    /// Begin a render pass. In lazy mode, this only stores the request
    /// without binding the FBO. The actual GPU setup is deferred to activate().
    virtual bool begin(const OffscreenRenderRequest &request) = 0;

    /// Ensure the render target is activated (FBO bound, viewport set).
    /// Called before the first draw command. No-op if already activated.
    virtual void activate() = 0;

    /// Whether the render target has been activated (FBO bound).
    virtual bool isActivated() const = 0;

    /// End the render pass and restore previous GL state.
    virtual void end() = 0;

    virtual SharedImageResource getImageResource() const = 0;
};