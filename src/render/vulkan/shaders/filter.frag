#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uTexture;

layout(std140, binding = 1) uniform FilterUBO
{
    vec4 directionRadiusDecal; // texel step xy, radius, decal
    vec4 colorAdjustment;      // saturation, brightness, contrast, grain
    vec4 options;              // apply color adjustment, reserved
    vec4 packedWeights[17];    // 65 taps packed into 68 floats
} ubo;

float weightAt(int index)
{
    vec4 packed = ubo.packedWeights[index / 4];
    int component = index % 4;
    if (component == 0) {
        return packed.x;
    }
    if (component == 1) {
        return packed.y;
    }
    if (component == 2) {
        return packed.z;
    }
    return packed.w;
}

vec4 sampleStraight(vec2 uv, bool decal)
{
    if (decal && (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)) {
        return vec4(0.0);
    }
    return texture(uTexture, uv);
}

void main()
{
    int radius = int(ubo.directionRadiusDecal.z + 0.5);
    bool decal = ubo.directionRadiusDecal.w > 0.5;
    vec2 direction = ubo.directionRadiusDecal.xy;

    vec4 center = sampleStraight(vUV, decal);
    vec4 sum = vec4(center.rgb * center.a, center.a) * weightAt(0);
    for (int i = 1; i <= 64; ++i) {
        if (i > radius) {
            break;
        }
        vec2 offset = direction * float(i);
        vec4 lo = sampleStraight(vUV - offset, decal);
        vec4 hi = sampleStraight(vUV + offset, decal);
        float weight = weightAt(i);
        sum += vec4(lo.rgb * lo.a, lo.a) * weight;
        sum += vec4(hi.rgb * hi.a, hi.a) * weight;
    }

    vec4 color = sum.a > 0.000001
        ? vec4(sum.rgb / sum.a, sum.a)
        : vec4(0.0);

    if (ubo.options.x > 0.5 && color.a > 0.000001) {
        float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        color.rgb = vec3(luma) + (color.rgb - vec3(luma)) * ubo.colorAdjustment.x;
        color.rgb = ((color.rgb - vec3(0.5)) * ubo.colorAdjustment.z + vec3(0.5))
            * ubo.colorAdjustment.y;
        color.rgb = clamp(color.rgb, 0.0, 1.0);
    }

    float grain = ubo.colorAdjustment.w;
    if (grain > 0.0 && color.a > 0.000001) {
        float noise = fract(sin(dot(gl_FragCoord.xy,
            vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
        color.rgb = clamp(color.rgb + vec3(noise * grain), 0.0, 1.0);
    }
    outColor = color;
}
