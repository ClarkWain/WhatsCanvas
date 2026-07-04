#version 450

// M5 textured-quad fragment shader. Samples a combined image sampler.

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform Push
{
    float layerAlpha;
} pc;

void main()
{
    vec4 sampled = texture(uTexture, vUV);
    outColor = vec4(sampled.rgb, sampled.a * pc.layerAlpha);
}
