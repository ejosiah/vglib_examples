#version 460

#extension GL_EXT_debug_printf : require

#include "path_tracing/eval_brdf.glsl"
#include "domain.glsl"

layout(location = 0) rayPayloadIn HitRecord hRec;

hitAttribute vec2 attribs;

void main() {

    vec3 p0 = gl_HitTriangleVertexPositionsEXT[0];
    vec3 p1 = gl_HitTriangleVertexPositionsEXT[1];
    vec3 p2 = gl_HitTriangleVertexPositionsEXT[2];

    vec3 e0 = p1 - p0;
    vec3 e1 = p2 - p0;

    vec3 n = normalize(cross(e0, e1));

    float u = 1 - attribs.x - attribs.y;
    float v = attribs.x;
    float w = attribs.y;

    vec3 p = p0 * u + p1 * v + p2 * w;

    mat3 nmat = inverse(transpose(mat3(gl_ObjectToWorld)));

    vec3 wp = gl_ObjectToWorld * vec4(p, 1);
    vec3 wn = gl_ObjectToWorld * vec4(n, 0);

    ObjectInfo object = objects[gl_InstanceID];
    hRec.isect.xLocal = p;
    hRec.isect.nLocal = n;
    hRec.isect.x = wp;
    hRec.isect.gN = wn;
    hRec.isect.sN = wn;
    hRec.isect.isFrontFacing = gl_HitKind == gl_HitKindFrontFacingTriangle;
    hRec.isect.medium = object.mediumId;
    hRec.isect.material = object.materialId;
    hRec.t = gl_HitT;
    hRec.wo = -gl_WorldRayDirection;
}