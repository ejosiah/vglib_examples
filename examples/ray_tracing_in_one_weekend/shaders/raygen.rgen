#version 460
#extension GL_EXT_ray_tracing : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"

layout(set = 0, binding = 0) uniform accelerationStructure topLevelAs;
layout(set = 0, binding = 2, rgba8) uniform image2D image;

layout(location = 0) rayPayload HitRecord hRec;

const float Tmin = 0.001;
const float Tmax = 10000.0;

void init(out HitRecord hRec, vec2 offset);

void generateRay(out vec3 origin, out vec3 direction, vec2 offset);

vec3 computeRadience(uint currentSample, uint sampleCount);

void main(){

    hRec.rngState = initRNG(vec2(gl_LaunchID), vec2(gl_LaunchSize), frame);

    vec3 fcolor = vec3(0);

    if(adaptiveSampling == 1) {
        if(currentSample >= sampleCount) return;

        fcolor = computeRadience(currentSample, sampleCount);
        vec3 dst = imageLoad(image, ivec2(gl_LaunchID.xy)).rgb;
        float t = 1/float(currentSample + 1);
        fcolor = mix(dst, fcolor, t);
    }
    else {
        for (uint i = 0; i < sampleCount; ++i) {
            fcolor += computeRadience(i, sampleCount);
        }
        fcolor /= sampleCount;
    }

    imageStore(image, ivec2(gl_LaunchID.xy), vec4(fcolor, 1));

}

vec3 computeRadience(uint currentSample, uint sampleCount) {
    vec2 offset = hammersley(currentSample, sampleCount);
    generateRay(hRec.x, hRec.wi, offset);

    vec3 throughput = vec3(1);
    vec3 color = vec3(0);
    hRec.hit = true;
    hRec.color = vec3(0);
    hRec.attenuation = vec3(1);

    for(int bounce = 0; bounce < maxBounce; ++bounce) {
        traceRayEXT(topLevelAs, gl_RayFlagsOpaqueEXT, 0xff, 0, 1, 0, hRec.x, Tmin, hRec.wi, Tmax, 0);
        if(!hRec.hit || isBlack(hRec.wi)) {
            color += throughput * hRec.color;
            break;
        }
        throughput *= hRec.attenuation;

    }
    return color;
}

void generateRay(out vec3 origin, out vec3 direction, vec2 offset) {
    const vec2 pixelCenter = vec2(gl_LaunchID.xy) + offset;
    const vec2 inUV = pixelCenter/vec2(gl_LaunchSize.xy);
    vec2 d = inUV * 2.0 - 1.0;

    origin = mat4x3(viewInverse) * vec4(0,0,0,1);
    vec4 target = projInverse * vec4(d.x, d.y, 1, 1) ;
    direction = mat3(viewInverse) * normalize(target.xyz) ;
}

void init(out HitRecord hRec, vec2 offset) {
    generateRay(hRec.x, hRec.wi, offset);
    hRec.hit = true;
    hRec.color = vec3(0);
}