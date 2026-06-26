#pragma once

namespace wsc::opengl {

inline const char *shaderVersionDirective()
{
#if defined(WHATSCANVAS_OPENGL_ES)
    return "#version 300 es\nprecision mediump float;\nprecision mediump int;\n";
#else
    return "#version 330 core\n";
#endif
}

} // namespace wsc::opengl
