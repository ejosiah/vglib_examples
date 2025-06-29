#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_tracing_position_fetch : require

#include "ray_tracing_lang.glsl"

struct Vertex{
    vec3 position;
    vec3 color;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec2 uv;
};

layout(set = 1, binding = 0) buffer VERETX_INFO {
    Vertex v[];
} vertices[3];

layout(set = 1, binding = 1) buffer INDEX_INFO {
    uint i[];
} index[3];

layout(shaderRecord) buffer block {
    vec4 color;
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec3 attribs;

void main() {

    float u = 1 - attribs.x - attribs.y;
    float v = attribs.x;
    float w = attribs.y;

    ivec3 index = ivec3(
        index[0].i[3 * gl_PrimitiveID + 0],
        index[0].i[3 * gl_PrimitiveID + 1],
        index[0].i[3 * gl_PrimitiveID + 2]
    );

    Vertex v0 = vertices[0].v[index.x];
    Vertex v1 = vertices[0].v[index.y];
    Vertex v2 = vertices[0].v[index.z];

    vec3 n = v0.normal * u + v1.normal * v + v2.normal * w;

    vec3 eyes = gl_WorldRayOrigin;
    vec3 worldPos = gl_WorldRayOrigin + gl_HitT * gl_WorldRayDirection;
    vec3 N = normalize(n);
    vec3 lightPos = eyes;
    vec3 lightDir = lightPos - worldPos;
    float lightDistance = length(lightDir);
    vec3 E = normalize(eyes - worldPos);
    vec3 L = normalize(lightDir);
    vec3 H = normalize(E + L);

    float shininess = 50;
    float attenuation = 1.0;

    vec3 diffuseColor = color.rgb * max(0, dot(N, L));
    hitValue = diffuseColor * attenuation;
}