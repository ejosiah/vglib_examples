#version 450

#extension GL_EXT_debug_printf : enable


#include "terrain_ubo.glsl"

layout(quads, fractional_even_spacing, ccw) in;

layout(set = 0, binding = 1) uniform sampler2D displacementMap;
layout(set = 0, binding = 2) uniform sampler2D normalMap;

layout(location = 0) in vec2 uv_in[];
layout(location = 1) in vec3 normal_in[];
layout(location = 2) in vec3 color_in[];

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 uv_out;
layout(location = 3) out vec2 patch_uv_out;
layout(location = 4) out vec3 color_out;

bool isEdge(vec2 uv){
    return uv.x == 0 || uv.x == 1 || uv.y == 0 || uv.y == 1;
}

float remap(float x, float a, float b, float c, float d){
    float t = clamp((x - a)/(b - a), 0, 1);
    return mix(c, d, t);
}

void main(){
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec4 p0 = gl_in[0].gl_Position;
    vec4 p1 = gl_in[1].gl_Position;
    vec4 p2 = gl_in[2].gl_Position;
    vec4 p3 = gl_in[3].gl_Position;

    vec4 p = mix(mix(p0, p1, u), mix(p3, p2, u), v);


    vec2 uv0 = uv_in[0];
    vec2 uv1 = uv_in[1];
    vec2 uv2 = uv_in[2];
    vec2 uv3 = uv_in[3];


    vec2 uv = mix(mix(uv0, uv1, u), mix(uv3, uv2, u), v);
    float y = remap(texture(displacementMap, uv).r, 0, 1, minZ, maxZ) * heightScale;

    p.y += y;

    normal = 2 * texture(normalMap, uv).xyz - 1;

    worldPosition = p.xyz;
    uv_out = uv;
    patch_uv_out = vec2(u, v);

    vec3 c0 = color_in[0];
    vec3 c1 = color_in[1];
    vec3 c2 = color_in[2];
    vec3 c3 = color_in[3];
    color_out = mix(mix(c0, c1, u), mix(c3, c2, u), v);


    gl_Position = p;
}