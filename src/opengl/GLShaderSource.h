#pragma once

namespace wsc::opengl {

inline const char *shaderVersionDirective()
{
#if defined(WHATSCANVAS_OPENGL_ES)
    // Mirror the C++ backend selection into shader source.  Individual shaders
    // can use this to omit desktop-only features such as dual-source blending.
    return "#version 300 es\n#define WHATSCANVAS_OPENGL_ES 1\nprecision mediump float;\nprecision mediump int;\n";
#else
    return "#version 330 core\n";
#endif
}

// Global uniforms every fragment shader declares so it can honour an active
// anti-aliased clip coverage mask. When uClipEnabled == 0 the shader behaves
// exactly as before (no clip); otherwise it multiplies its output alpha by the
// coverage sampled from uClipMask at the fragment's device position.
inline const char *clipMaskFragmentUniforms()
{
    return "uniform sampler2D uClipMask;\n"
           "uniform int uClipEnabled;\n"
           // The mask covers the full canvas. Offscreen replay translates the
           // viewport, so framebuffer coordinates must be translated back.
           // Explicit GLES precision also preserves DrawPath's highp math when
           // this helper is declared before that shader's precision statement.
#if defined(WHATSCANVAS_OPENGL_ES)
           "uniform highp vec2 uClipViewport;\n"
           "uniform highp vec2 uClipOffset;\n"
           "highp float clipMaskCoverage() {\n"
           "    highp vec2 position = gl_FragCoord.xy - uClipOffset;\n"
#else
           "uniform vec2 uClipViewport;\n"
           "uniform vec2 uClipOffset;\n"
           "float clipMaskCoverage() {\n"
           "    vec2 position = gl_FragCoord.xy - uClipOffset;\n"
#endif
           "    return texture(uClipMask, position / uClipViewport).r;\n"
           "}\n";
}

} // namespace wsc::opengl
