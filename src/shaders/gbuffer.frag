#version 460

#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) in vec3 v_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec4 v_tangent;
layout(location = 3) in vec2 v_texCoord;

layout(location = 0) out vec4 albedoMetallicOut;
layout(location = 1) out vec4 normalRoughnessOut;
layout(location = 2) out vec4 emissiveOut;

layout(set = 0, binding = 1) uniform sampler2D albedo;
layout(set = 0, binding = 2) uniform sampler2D metallicRoughness;
layout(set = 0, binding = 3) uniform sampler2D normalMap;
layout(set = 0, binding = 4) uniform sampler2D emissive;

void main() 
{
    albedoMetallicOut.rgb = texture(albedo, v_texCoord).rgb;
    albedoMetallicOut.a = texture(metallicRoughness, v_texCoord).b;

    vec3 normal = normalize(v_normal);
    // TODO remove normalize tangent?
    vec3 tangent = normalize(v_tangent.xyz);
    vec3 bitangent = cross(normal.xyz, tangent) * v_tangent.w;

    vec3 perturb = normalize(texture(normalMap, v_texCoord).xyz * 2.0 - 1.0);
    // TODO remove this normalize?
    vec3 n = normalize(normal * perturb.z + tangent * perturb.x + bitangent * perturb.y);

    normalRoughnessOut.rgb = (n + 1.0) * 0.5;
    normalRoughnessOut.a = texture(metallicRoughness, v_texCoord).g;

    //emissiveOut = texture(emissive, v_texCoord);
}