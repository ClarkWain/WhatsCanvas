#version 450

// Common image/glyph path: straight-alpha texture, no clip, no color matrix.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vTint;
layout(location = 2) in vec3 vRounded;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform Push
{
    mat4 colorMatrix;
    vec4 tint;
    vec4 colorOffset;
    float layerAlpha;
    int useColorMatrix;
    int sourcePremultiplied;
    int useClipMask;
    vec2 clipUvScale;
    vec2 clipUvOffset;
} pc;

void main()
{
    vec4 sampled = texture(uTexture, vUV);
    vec4 tint = pc.tint * vTint;
    outColor = vec4(
        sampled.rgb * tint.rgb,
        sampled.a * tint.a * pc.layerAlpha);
}
