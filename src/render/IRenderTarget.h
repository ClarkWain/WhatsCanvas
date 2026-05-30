#pragma once

#include "RenderTypes.h"

struct OffscreenRenderRequest;

class IRenderTarget
{
public:
    virtual ~IRenderTarget() = default;

    virtual bool isValid() const = 0;
    virtual bool begin(const OffscreenRenderRequest &request) = 0;
    virtual void end() = 0;
    virtual SharedImageResource getImageResource() const = 0;
};