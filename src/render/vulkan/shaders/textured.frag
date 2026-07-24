#version 450

// Textured-quad fragment shader. Samples a combined image sampler and applies an
// optional tint, 4x4 color matrix (+ offset), and a layer-alpha multiplier.

layout(location = 0) in vec2 vUV;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform Push
{
    mat4 colorMatrix;   // offset 0
    vec4 tint;          // offset 64
    vec4 colorOffset;   // offset 80
    float layerAlpha;   // offset 96
    int useColorMatrix; // offset 100
    int sourcePremultiplied; // offset 104
} pc;

void main()
{
    vec4 sampled = texture(uTexture, vUV);
    if (pc.sourcePremultiplied != 0 && sampled.a > 0.000001)
    {
        sampled.rgb /= sampled.a;
    }
    vec4 c = vec4(sampled.rgb * pc.tint.rgb, sampled.a * pc.tint.a);
    if (pc.useColorMatrix != 0)
    {
        c = pc.colorMatrix * c + pc.colorOffset;
    }
    outColor = vec4(c.rgb, c.a * pc.layerAlpha);
}
