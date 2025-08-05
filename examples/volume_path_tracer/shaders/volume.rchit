#version 460

#include "path_tracing/eval_brdf.glsl"
#include "domain.glsl"

layout(location = 1) rayPayloadIn MediumHitRecord mRec;

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
    mRec.isect.xLocal = p;
    mRec.isect.nLocal = n;
    mRec.isect.x = wp;
    mRec.isect.gN = wn;
    mRec.isect.sN = wn;
    mRec.isect.isFrontFacing = gl_HitKind == gl_HitKindFrontFacingTriangle;
    mRec.isect.medium = object.mediumId;
    mRec.isect.material = object.materialId;
    mRec.t = gl_HitT;
}