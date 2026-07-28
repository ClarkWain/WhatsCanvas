#version 450

// M5 textured-quad vertex shader. Position in NDC, UV in [0,1].

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inTint;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vTint;
layout(location = 2) out vec3 vRounded;

void main()
{
    gl_Position = vec4(inPosition, 0.0, 1.0);
    vUV = inUV;
    vTint = inTint;
    vRounded = vec3(0.0);
}
