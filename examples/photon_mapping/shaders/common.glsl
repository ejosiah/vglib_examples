#ifndef UTIL_GLSL
#define UTIL_GLSL

#include "rt_constants.glsl"
#include "sampling.glsl"
#include "ray_tracing_lang.glsl"
#include "scene_objects.glsl"
#include "structs.glsl"
#include "random.glsl"

uint numSamples = 100;
uint numShadowSamples = 50;

vec3 offsetRay(in vec3 p, in vec3 n)
{
    const float intScale   = 256.0f;
    const float floatScale = 1.0f / 65536.0f;
    const float origin     = 1.0f / 32.0f;

    ivec3 of_i = ivec3(intScale * n.x, intScale * n.y, intScale * n.z);

    vec3 p_i = vec3(intBitsToFloat(floatBitsToInt(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
    intBitsToFloat(floatBitsToInt(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
    intBitsToFloat(floatBitsToInt(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));

    return vec3(abs(p.x) < origin ? p.x + floatScale * n.x : p_i.x, //
    abs(p.y) < origin ? p.y + floatScale * n.y : p_i.y, //
    abs(p.z) < origin ? p.z + floatScale * n.z : p_i.z);
}

vec3 computeRadiance(HitData hit, vec3 lightPos) {
    vec3 eyes = gl_WorldRayOrigin;
    vec3 lightDir = lightPos - hit.position;
    vec3 L = normalize(lightDir);

    vec3 N = normalize(hit.normal);
    vec3 E = normalize(eyes - hit.position);
    vec3 H = normalize(E + L);
    vec3 R = reflect(-L, N);

    float dist = length(lightDir);
    vec3 radiance = max(0, dot(light.normal, -L)) * light.intensity /(dist * dist);
    radiance;

    return hit.material.emission + radiance * max(0, dot(L, N)) * hit.material.diffuse;
}

float computeVisiblity(HitData hit) {

    const uint rayFlags =  gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHit | gl_RayFlagsSkipClosestHitShader;
    vec3 origin = offsetRay(hit.position, hit.normal);
    vec3 dim = light.upperCorner - light.lowerCorner;

    float visibility = 0;
    uint N = numShadowSamples;
    for(uint i = 0; i < N; i++) {
        isVisible = false;
        vec2 point = hammersley(i, N);
        //        vec2 point = vec2(rand(rtParams.rngState), rand(rtParams.rngState));
        vec3 lightPos = light.lowerCorner + dim * vec3(point.x, 0, point.y);

        vec3 direction = lightPos - origin;
        const float tMax = length(direction);
        direction = normalize(direction);

        traceRay(topLevelAS, rayFlags, mask & ~OBJ_MASK_LIGHTS, 0, 0, SHADOW_MISS, origin, 0, direction, tMax, 1);
        visibility += isVisible ? 1 : 0;
    }

    return visibility / float(N);
}

#endif // UTIL_GLSL
