#pragma once

class GLProgram;
class RenderContext;

namespace wsc::opengl {

/// Sets the clip-mask uniforms (uClipEnabled, uClipMask, uClipViewport) that
/// every draw program declares, so an active anti-aliased clip coverage mask is
/// honoured. When no clip is active this sets uClipEnabled = 0 and the shader
/// behaves identically to having no clip.
void applyClipMaskUniforms(GLProgram *program, const RenderContext &context);

} // namespace wsc::opengl
