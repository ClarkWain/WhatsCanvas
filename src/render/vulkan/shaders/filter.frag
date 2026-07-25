#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uTexture;
layout(binding = 2) uniform sampler2D uOriginalTexture;

layout(std140, binding = 1) uniform FilterUBO
{
    vec4 directionRadiusDecal; // texel step xy, radius, decal
    vec4 colorAdjustment;      // saturation, brightness, contrast, grain
    vec4 options;              // color adjustment, source premul, output straight, straight resample
    vec4 innerShadow;          // offset xy, enabled, original premultiplied
    vec4 innerShadowColor;
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

vec4 fetchStraightPremultiplied(ivec2 coord, bool decal)
{
    ivec2 size = textureSize(uTexture, 0);
    if (decal && (coord.x < 0 || coord.x >= size.x
                  || coord.y < 0 || coord.y >= size.y)) {
        return vec4(0.0);
    }
    ivec2 bounded = clamp(coord, ivec2(0), size - ivec2(1));
    vec4 color = texelFetch(uTexture, bounded, 0);
    return vec4(color.rgb * color.a, color.a);
}

vec4 samplePremultiplied(vec2 uv, bool decal, bool sourcePremultiplied,
                         bool resampleStraightAlpha)
{
    if (sourcePremultiplied) {
        if (decal && (uv.x < 0.0 || uv.x > 1.0
                      || uv.y < 0.0 || uv.y > 1.0)) {
            return vec4(0.0);
        }
        return texture(uTexture, uv);
    }
    if (!resampleStraightAlpha) {
        if (decal && (uv.x < 0.0 || uv.x > 1.0
                      || uv.y < 0.0 || uv.y > 1.0)) {
            return vec4(0.0);
        }
        vec4 color = texture(uTexture, uv);
        return vec4(color.rgb * color.a, color.a);
    }
    vec2 size = vec2(textureSize(uTexture, 0));
    vec2 position = uv * size - vec2(0.5);
    ivec2 base = ivec2(floor(position));
    vec2 fraction = fract(position);
    vec4 c00 = fetchStraightPremultiplied(base, decal);
    vec4 c10 = fetchStraightPremultiplied(base + ivec2(1, 0), decal);
    vec4 c01 = fetchStraightPremultiplied(base + ivec2(0, 1), decal);
    vec4 c11 = fetchStraightPremultiplied(base + ivec2(1, 1), decal);
    return mix(mix(c00, c10, fraction.x),
               mix(c01, c11, fraction.x), fraction.y);
}

float sampleAlphaDecal(vec2 uv)
{
    ivec2 size = textureSize(uTexture, 0);
    vec2 position = uv * vec2(size) - vec2(0.5);
    ivec2 base = ivec2(floor(position));
    vec2 fraction = fract(position);
    float samples[4];
    ivec2 taps[4] = ivec2[4](
        base, base + ivec2(1, 0),
        base + ivec2(0, 1), base + ivec2(1, 1));
    for (int i = 0; i < 4; ++i) {
        ivec2 tap = taps[i];
        samples[i] =
            tap.x < 0 || tap.x >= size.x
            || tap.y < 0 || tap.y >= size.y
            ? 0.0 : texelFetch(uTexture, tap, 0).a;
    }
    return mix(mix(samples[0], samples[1], fraction.x),
               mix(samples[2], samples[3], fraction.x), fraction.y);
}

void main()
{
    int radius = int(ubo.directionRadiusDecal.z + 0.5);
    bool decal = ubo.directionRadiusDecal.w > 0.5;
    bool sourcePremultiplied = ubo.options.y > 0.5;
    bool outputStraight = ubo.options.z > 0.5;
    bool resampleStraightAlpha = ubo.options.w > 0.5;
    vec2 direction = ubo.directionRadiusDecal.xy;

    vec4 sum =
        samplePremultiplied(vUV, decal, sourcePremultiplied,
                            resampleStraightAlpha) * weightAt(0);
    for (int i = 1; i <= 64; ++i) {
        if (i > radius) {
            break;
        }
        vec2 offset = direction * float(i);
        float weight = weightAt(i);
        sum += samplePremultiplied(
            vUV - offset, decal, sourcePremultiplied,
            resampleStraightAlpha) * weight;
        sum += samplePremultiplied(
            vUV + offset, decal, sourcePremultiplied,
            resampleStraightAlpha) * weight;
    }

    if (ubo.innerShadow.z > 0.5) {
        vec4 original = texture(uOriginalTexture, vUV);
        if (ubo.innerShadow.w > 0.5 && original.a > 0.000001) {
            original.rgb /= original.a;
        }
        if (original.a <= 0.000001) {
            outColor = vec4(0.0);
            return;
        }
        vec2 shadowUV = vUV - ubo.innerShadow.xy;
        float blurredAlpha = sampleAlphaDecal(shadowUV);
        float coverage =
            clamp((original.a - blurredAlpha) / original.a, 0.0, 1.0)
            * ubo.innerShadowColor.a;
        outColor = vec4(
            mix(original.rgb, ubo.innerShadowColor.rgb, coverage),
            original.a);
        return;
    }

    if (!outputStraight) {
        outColor = sum;
        return;
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
