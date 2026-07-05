#version 450

// Fragment-evaluated gradient (linear/radial, up to 8 stops) matching the
// OpenGL DrawPath gradient shader: applyGradientTile + sampleGradient.

layout(location = 0) in vec2 vLocal;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform GradientUBO
{
    vec4 linear;       // start.xy, end.xy
    vec4 radial;       // center.xy, radius, type (1=linear, 2=radial)
    ivec4 modeCount;   // tileMode, stopCount, _, _
    vec4 stopPos0;     // positions 0..3
    vec4 stopPos1;     // positions 4..7
    vec4 stopColors[8];
} g;

float stopPos(int i)
{
    return i < 4 ? g.stopPos0[i] : g.stopPos1[i - 4];
}

float applyGradientTile(float t, out float visibility)
{
    visibility = 1.0;
    int mode = g.modeCount.x;
    if (mode == 1) { // repeat
        return fract(t);
    }
    if (mode == 2) { // mirror
        float period = floor(t);
        float localT = t - period;
        if (mod(abs(period), 2.0) > 0.5) {
            localT = 1.0 - localT;
        }
        return localT;
    }
    if (mode == 3) { // decal
        visibility = (t >= 0.0 && t <= 1.0) ? 1.0 : 0.0;
        return clamp(t, 0.0, 1.0);
    }
    return clamp(t, 0.0, 1.0); // clamp
}

vec4 sampleGradient(float t)
{
    float visibility = 1.0;
    t = applyGradientTile(t, visibility);
    int stopCount = g.modeCount.y;
    if (visibility <= 0.0 || stopCount <= 0) {
        return vec4(0.0);
    }
    if (stopCount == 1 || t <= stopPos(0)) {
        return g.stopColors[0] * visibility;
    }
    for (int i = 1; i < 8; ++i) {
        if (i >= stopCount) {
            break;
        }
        if (t <= stopPos(i)) {
            float startPos = stopPos(i - 1);
            float endPos = stopPos(i);
            float span = max(endPos - startPos, 0.0001);
            float localT = clamp((t - startPos) / span, 0.0, 1.0);
            return mix(g.stopColors[i - 1], g.stopColors[i], localT) * visibility;
        }
    }
    return g.stopColors[stopCount - 1] * visibility;
}

void main()
{
    int type = int(g.radial.w);
    float t;
    if (type == 2) {
        t = length(vLocal - g.radial.xy) / max(g.radial.z, 0.0001);
    } else {
        vec2 direction = g.linear.zw - g.linear.xy;
        float lengthSq = max(dot(direction, direction), 0.0001);
        t = dot(vLocal - g.linear.xy, direction) / lengthSq;
    }
    outColor = sampleGradient(t);
}
