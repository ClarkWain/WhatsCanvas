#version 450

// M7 clip fragment shader: modulate the fill alpha by the clip coverage sampled
// from a single-channel-in-R mask.

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uClipMask;

void main()
{
    float coverage = texture(uClipMask, vUV).r;
    outColor = vec4(vColor.rgb, vColor.a * coverage);
}
