#include "ClipMaskUniforms.h"

#include <glm/glm.hpp>

#include "GLProgram.h"
#include "render/RenderContext.h"

namespace wsc::opengl {

void applyClipMaskUniforms(GLProgram *program, const RenderContext &context)
{
    if (program == nullptr) {
        return;
    }
    const bool clipActive = context.isClipMaskActive();
    program->setInt("uClipEnabled", clipActive ? 1 : 0);
    if (!clipActive) {
        return;
    }
    program->setInt("uClipMask", context.clipMaskTextureUnit());
    program->setVec2("uClipViewport",
                     glm::vec2(static_cast<float>(context.getWidth()),
                               static_cast<float>(context.getHeight())));
}

} // namespace wsc::opengl
