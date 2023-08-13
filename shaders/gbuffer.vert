#version 460
#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 fragColor;

layout(binding = 0) uniform CameraUniforms {
    mat4 model;
    mat4 view;
    mat4 proj;
};

void main() {
    gl_Position = proj * view * model * vec4(position, 1.0);
    fragColor = normal;
}