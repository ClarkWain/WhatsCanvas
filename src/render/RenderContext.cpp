#include "render/RenderContext.h"

#include "command/DrawData.h"
#include "command/DrawPath.h"

namespace {

std::uint64_t makeClipStateKey(const ClipMaskState &clipMask, const ScissorState &scissor)
{
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;
    std::uint64_t hash = clipMask.fingerprint == 0 ? 1469598103934665603ull : clipMask.fingerprint;
    const std::uint64_t scissorValues[5] = {
        scissor.enabled ? 1ull : 0ull,
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(scissor.x)),
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(scissor.y)),
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(scissor.width)),
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(scissor.height))
    };

    for (std::uint64_t value : scissorValues) {
        hash ^= value;
        hash *= kFnvPrime;
    }

    return hash;
}

} // namespace

void RenderContext::applyClipState(const ScissorState &scissor, const ClipMaskState &clipMask) const
{
    applyScissorState(scissor);

    if (!clipMask.hasPaths()) {
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
        clearClipMask();
        return;
    }

    const std::uint64_t clipKey = makeClipStateKey(clipMask, scissor);
    if (isClipMaskCurrent(clipKey)) {
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0x00);
        glStencilFunc(GL_EQUAL, static_cast<GLint>(clipMask.pathCount()), 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        return;
    }

    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    for (size_t clipIndex = 0; clipIndex < clipMask.resources.size(); ++clipIndex) {
        const auto &clipResource = clipMask.resources[clipIndex];
        if (!clipResource || !clipResource->isValid()) {
            continue;
        }

        clipResource->apply(*this, scissor, clipIndex);
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, static_cast<GLint>(clipMask.pathCount()), 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    rememberClipMask(clipKey);
}