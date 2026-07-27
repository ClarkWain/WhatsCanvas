#version 450

// Axis-aligned textured quads use one compact instance instead of repeating
// six full vertices. Non-axis-aligned geometry stays on textured.vert.

layout(location = 0) in vec4 inBounds;
layout(location = 1) in vec4 inUVBounds;
layout(location = 2) in vec4 inTint;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vTint;

const vec2 kCorners[4] = vec2[4](
    vec2(0.0, 0.0), vec2(1.0, 0.0),
    vec2(0.0, 1.0), vec2(1.0, 1.0));

void main()
{
    vec2 corner = kCorners[gl_VertexIndex];
    vec2 position = mix(inBounds.xy, inBounds.zw, corner);
    gl_Position = vec4(position, 0.0, 1.0);
    vUV = mix(inUVBounds.xy, inUVBounds.zw, corner);
    vTint = inTint;
}
