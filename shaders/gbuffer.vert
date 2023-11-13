#version 460

#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec3 positionOut;
layout(location = 1) out vec3 normalOut;
layout(location = 2) out vec2 texCoordOut;

layout(set = 0, binding = 0, std140) uniform Uniforms 
{
    mat4 model;
    mat4 view;
    mat4 proj;
};

void main() 
{
    vec4 worldPos = model * vec4(position, 1.0);
    gl_Position = proj * view * worldPos;

    positionOut = worldPos.xyz;
    normalOut = transpose(inverse(mat3(model))) * normal;
    texCoordOut = texCoord;
}