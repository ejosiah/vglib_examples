#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ray_tracing_lang.glsl"
#include "scene_objects.glsl"
#include "structs.glsl"


layout(location = 0) rayPayloadIn HitRecord record;

hitAttribute vec2 attribs;

void main() {
    HitData hit = getHitData(attribs);

    record.hit = true;
    record.meshId = gl_InstanceCustomIndex;
    record.position = hit.position;
    record.TBN = mat3(hit.tangent, hit.bitangent, hit.normal);

}