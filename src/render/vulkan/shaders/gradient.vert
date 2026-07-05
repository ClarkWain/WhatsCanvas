#version 450

// Gradient vertex shader: NDC position + canvas-space local position (used to
// evaluate the gradient parameter in the fragment shader).

layout(location = 0) in vec2 inPosition; // NDC
layout(location = 1) in vec2 inLocal;    // canvas-space local position

layout(location = 0) out vec2 vLocal;

void main()
{
    gl_Position = vec4(inPosition, 0.0, 1.0);
    vLocal = inLocal;
}
