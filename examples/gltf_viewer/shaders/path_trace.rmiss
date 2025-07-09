#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

#include "domain.glsl"

layout(location = 0) rayPayloadIn HitRecord hRec;

void main() {
    hRec.hit = false;
    vec2 uv = .5 + .5 * octEncode(normalize(hRec.wi));
    hRec.brdfWeigth = texture(global_textures[environment], uv).rgb * ibl_intensity;
}