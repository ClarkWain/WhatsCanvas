#version 450

// M3 solid-color vertex shader. Positions are provided in Vulkan NDC.
// Per-vertex color lets the same pipeline serve solid fills now and vertex-color
// gradients later.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 vColor;

void main()
{
    gl_Position = vec4(inPosition, 0.0, 1.0);
    gl_PointSize = 1.0;
    vColor = inColor;
}
