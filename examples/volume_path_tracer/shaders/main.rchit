#version 460

#include "path_tracing/eval_brdf.glsl"
#include "domain.glsl"

layout(location = 0) rayPayloadIn HitRecord hRec;

hitAttribute vec2 attibs;

void main() {
    vec3 p0 = gl_ObjectToWorld * vec4(gl_HitTriangleVertexPositionsEXT[0], 1);
    vec3 p1 = gl_ObjectToWorld * vec4(gl_HitTriangleVertexPositionsEXT[1], 1);
    vec3 p2 = gl_ObjectToWorld * vec4(gl_HitTriangleVertexPositionsEXT[2], 1);

    vec3 e0 = p1 - p0;
    vec3 e1 = p2 - p0;

    vec3 n = normalize(cross(e0, e1));

    float u = 1 - attibs.x - attibs.y;
    float v = attibs.x;
    float w = attibs.y;

    vec3 p = p0 * u + p1 * v + p2 * w;

    ObjectInfo object = objects[gl_InstanceID];
    hRec.isect.x = p;
    hRec.isect.gN = n;
    hRec.isect.sN = n;
    hRec.isect.isFrontFacing = gl_HitKind == gl_HitKindFrontFacingTriangle;
    hRec.isect.medium = object.mediumId;
    hRec.isect.material = object.materialId;
    hRec.t = gl_HitT;
    hRec.wo = -gl_WorldRayDirection;
}