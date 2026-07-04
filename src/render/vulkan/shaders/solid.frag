#version 450

// Solid-color fragment shader with analytic-AA coverage modulating alpha.

layout(location = 0) in vec4 vColor;
layout(location = 1) in float vCoverage;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(vColor.rgb, vColor.a * clamp(vCoverage, 0.0, 1.0));
}
