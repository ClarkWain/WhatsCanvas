#version 450

// M3 solid-color fragment shader.

layout(location = 0) in vec4 vColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vColor;
}
