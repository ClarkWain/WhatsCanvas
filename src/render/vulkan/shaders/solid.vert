#version 450

// Solid-color vertex shader. Per-vertex color + analytic-AA coverage. Positions
// are provided in Vulkan NDC.

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in float inCoverage;

layout(location = 0) out vec4 vColor;
layout(location = 1) out float vCoverage;

void main()
{
    gl_Position = vec4(inPosition, 0.0, 1.0);
    gl_PointSize = 1.0;
    vColor = inColor;
    vCoverage = inCoverage;
}
