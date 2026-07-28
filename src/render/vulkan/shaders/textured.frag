#version 450

// Textured-quad fragment shader. Samples a combined image sampler and applies an
// optional tint, 4x4 color matrix (+ offset), and a layer-alpha multiplier.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vTint;
layout(location = 2) in vec3 vRounded;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uTexture;
layout(binding = 1) uniform sampler2D uClipMask;

layout(push_constant) uniform Push
{
    mat4 colorMatrix;   // offset 0
    vec4 tint;          // offset 64
    vec4 colorOffset;   // offset 80
    float layerAlpha;   // offset 96
    int useColorMatrix; // offset 100
    int sourcePremultiplied; // offset 104
    int useClipMask;    // offset 108: 0 none, 1 texture, 2 rounded rect
    vec2 clipUvScale;   // texture scale, or rounded radius/width
    vec2 clipUvOffset;  // texture offset, or rounded height/unused
} pc;

float roundedRectCoverage()
{
    vec3 rounded = pc.useClipMask == 3
        ? vRounded
        : vec3(pc.clipUvScale.x, pc.clipUvScale.y, pc.clipUvOffset.x);
    float radius = rounded.x;
    if (radius <= 0.0)
    {
        return 1.0;
    }
    vec2 size = rounded.yz;
    vec2 halfSize = size * 0.5;
    radius = min(radius, min(halfSize.x, halfSize.y));
    vec2 point = vUV * size;
    vec2 q = abs(point - halfSize) - (halfSize - vec2(radius));
    float distanceToEdge =
        length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
    float aa = max(fwidth(distanceToEdge), 0.0001);
    return smoothstep(aa * 0.5, -aa * 0.5, distanceToEdge);
}

void main()
{
    vec4 sampled = texture(uTexture, vUV);
    if (pc.sourcePremultiplied != 0 && sampled.a > 0.000001)
    {
        sampled.rgb /= sampled.a;
    }
    vec4 tint = pc.tint * vTint;
    vec4 c = vec4(sampled.rgb * tint.rgb, sampled.a * tint.a);
    if (pc.useColorMatrix != 0)
    {
        c = pc.colorMatrix * c + pc.colorOffset;
    }
    float alpha = c.a * pc.layerAlpha;
    if (pc.useClipMask != 0)
    {
        if (pc.useClipMask == 1)
        {
            vec2 maskUV = gl_FragCoord.xy * pc.clipUvScale + pc.clipUvOffset;
            alpha *= texture(uClipMask, maskUV).r;
        }
        else
        {
            alpha *= roundedRectCoverage();
        }
    }
    outColor = vec4(c.rgb, alpha);
}
