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
    hRec.isect.id = gl_InstanceID;
    hRec.isect.medium = object.mediumId;
    hRec.isect.material = object.materialId;
//    vec3 albedo;
//
//    if(object.materialId == -1) {
//        albedo = checkerboard(p, n, 1);
//    }
//    Material mat = materials[object.materialId];
//
//    albedo = mat.diffuse.x != -1 ? mat.diffuse : checkerboard(p, n, 1);
//
//    vec3 wo = -gl_WorldRayDirection;
//    Surface s;
//    s.albedo = albedo;
//    s.transmission = vec3(1);
//    s.emission = vec3(0);
//    s.x = p;
//    s.gN = n;
//    s.sN = n;
//    s.roughness = clamp(mat.roughness, 0, 1);
//    s.metalness = clamp(mat.metalness, 0, 1);
//    s.opacity = 1.0;
//    s.inside = false;
//    s.volume = false;
//    s.ior = 1.0;
//    s.bsdf = mat.bsdf;
//
//    vec3 wi;
//    hRec.brdfWeight =  getBrdfWeight(s, hRec.rngState, wo, wi);
//    hRec.x = offsetRay(s.x, s.gN);
//    hRec.wi = wi;
}