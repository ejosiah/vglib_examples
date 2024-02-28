#version 460
#extension GL_EXT_ray_tracing : require

#include "ray_tracing_lang.glsl"
#include "rt_constants.glsl"
#include "structs.glsl"
#include "random.glsl"

layout(set = 0, binding = 0) uniform accelerationStructure topLevelAS;

layout(set = 0, binding = 1) uniform Uniforms {
    mat4 viewInverse;
    mat4 projInverse;
    uint mask;
};

layout(set = 0, binding = 2, rgba8) uniform image2D image;

layout(location = 0) rayPayload RtParams rtParams;

const int numBounces = 5;


void main(){

    const vec2 pixelCenter = vec2(gl_LaunchID.xy) + vec2(0.5);
    const vec2 inUV = pixelCenter/vec2(gl_LaunchSize.xy);
    vec2 d = inUV * 2.0 - 1.0;

    vec4 origin = viewInverse * vec4(0,0,0,1);
    vec4 target = projInverse * vec4(d.x, d.y, 1, 1);
    vec4 direction = viewInverse*vec4(normalize(target.xyz), 0) ;


    rtParams.rngState = initRNG(pixelCenter, vec2(1024, 700), 1u);
    rtParams.objectType = OBJECT_TYPE_GLASS;
    rtParams.ray.origin = origin.xyz;
    rtParams.ray.direction = direction.xyz;
    rtParams.ray.tMin = 0;
    rtParams.ray.tMax = 10000;
    rtParams.color = vec3(0.0);

    for(int bounce = 0; bounce < numBounces; bounce++) {
        if(rtParams.objectType != OBJECT_TYPE_MIRROR && rtParams.objectType != OBJECT_TYPE_GLASS) break;
        Ray ray = rtParams.ray;
        traceRay(topLevelAS, gl_RayFlagsOpaque, mask, 0, 0, MAIN_MISS, ray.origin, ray.tMin, ray.direction, ray.tMax, 0);

//        if(bounce == numBounces - 1 && (rtParams.objectType == OBJECT_TYPE_GLASS || rtParams.objectType == OBJECT_TYPE_MIRROR)){
//            rtParams.color = vec3(0, 1, 0);
//        }
    }

    imageStore(image, ivec2(gl_LaunchID.xy), vec4(rtParams.color, 0.0));

}