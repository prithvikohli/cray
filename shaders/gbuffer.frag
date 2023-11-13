#version 460
#extension GL_KHR_vulkan_glsl : enable

layout(location = 0) in vec3 pos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec4 albedoMetallicOut;
layout(location = 1) out vec4 normalRoughnessOut;
layout(location = 2) out vec4 emissiveOut;

layout(set = 0, binding = 1) uniform sampler2D albedo;
layout(set = 0, binding = 2) uniform sampler2D metallicRoughness;
layout(set = 0, binding = 3) uniform sampler2D normalMap;
layout(set = 0, binding = 4) uniform sampler2D emissive;

void main() 
{
    albedoMetallicOut.rgb = texture(albedo, texCoord).rgb;
    albedoMetallicOut.a = texture(metallicRoughness, texCoord).b;

    // construct tangent frame
    vec3 p0 = dFdx(pos);
    vec3 p1 = dFdy(pos);
    vec2 uv0 = dFdx(texCoord);
    vec2 uv1 = dFdy(texCoord);

    vec3 tangent = normalize(p0 * uv1.y - p1 * uv0.y);
    vec3 bitangent = normalize(-p0 * uv0.x + p1 *uv1.x);

    vec3 perturb = (texture(normalMap, texCoord).rgb * 2.0) - 1.0;
    vec3 n = normal * perturb.z + tangent * perturb.x + bitangent * perturb.y;

    normalRoughnessOut.rgb = (n + 1.0) * 0.5;
    normalRoughnessOut.a = texture(metallicRoughness, texCoord).g;

    emissiveOut = texture(emissive, texCoord);
}