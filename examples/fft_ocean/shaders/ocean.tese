#version 460

#extension GL_EXT_nonuniform_qualifier : require

#define PI 3.1415926535897
#define TWO_PI 6.283185307179586476925286766559


#include "scene.glsl"

#define HEIGHT_FIELD global_textures[scene.height_field_texture_id]


layout(quads, equal_spacing, ccw) in;

layout(set = 2, binding = 10) uniform sampler2D global_textures[];


layout(location = 0) in vec2 vUv[gl_MaxPatchVertices];
layout(location = 1) flat in int te_patchId[gl_MaxPatchVertices];

layout(location = 0) out vec3 worldPos;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 uv;
layout(location = 3) out int patchId;

vec3 F(float u, float v, out vec2 uv) {
    vec3 p0 = gl_in[0].gl_Position.xyz;
    vec3 p1 = gl_in[1].gl_Position.xyz;
    vec3 p2 = gl_in[2].gl_Position.xyz;
    vec3 p3 = gl_in[3].gl_Position.xyz;

    vec3 p = mix(mix(p0, p1, u), mix(p3, p2, u), v);

    uv = mix(mix(vUv[0], vUv[1], u), mix(vUv[3], vUv[2], u), v);

    vec3 offset = texture(HEIGHT_FIELD, uv).rgb;
    p.y += 2000;
    offset.y *= scene.amplitude;
    return p + offset;
}

vec3 computeNormal(vec2 uv) {
    float eps = 1e-5;

    float DFdx = texture(HEIGHT_FIELD, vec2(uv.x + eps, uv.y)).y - texture(HEIGHT_FIELD, vec2(uv.x - eps, uv.y)).y;
    float DFdy = texture(HEIGHT_FIELD, vec2(uv.x, uv.y + eps)).y - texture(HEIGHT_FIELD, vec2(uv.x, uv.y - eps)).y;

    vec3 gradient = vec3(DFdx, eps * 2, DFdy);
    gradient *= (0.5 * eps);
    return gradient;
}

void main(){

    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec3 p = F(u, v, uv);
    worldPos = p;
    float ep = 0.0001;

//    vec2 unused;
//    vec3 dFdx = (F(u + ep, v, unused) - F(u - ep, v, unused)) * ep * 0.5;
//    vec3 dFdy = (F(u, v + ep, unused) - F(u, v - ep, unused)) * ep * 0.5;
//    normal = normalize(cross(dFdx, dFdy));
    normal = computeNormal(uv);

    patchId = te_patchId[0];

    gl_Position =  scene.mvp * vec4(p, 1);
}