#ifndef LIGHTS_GLSL
#define LIGHTS_GLSL

#include "scene_push_constants.glsl"
#include "eval_brdf.glsl"
#include "medium.glsl"

layout(location = 1) rayPayload OcclusionData occData;

bool sampleLightRIS(inout RngStateType rngState, Sample _sample, Surface surface, out LightInfo selectedSampl, out float lightSampleWeight);

LightInfo sampleLight(inout RngStateType rngState, Sample _sample, Surface surface, out float lightWeight);

bool isVisisble(LightInfo light, Surface surface);

vec3 visibility(LightInfo light, Surface surface);

vec3 evalLightContribution(inout HitData hitData, vec3 wo);

vec3 evalLightContributionWithTranmission(inout HitData hitData, vec3 wo);

float fallOff(Light light, vec3 pointToLightDir);

vec3 evalBrdf(Surface surface, vec3 wo, vec3 wi);

bool sampleLightRIS(inout RngStateType rngState, Sample _sample, Surface surface, out LightInfo selectedSample, out float lightSampleWeight){
    if(numLights <= 0) return false;

    float totalWeights = 0;
    float samplePdfG = 0;

    for(int i = 0; i < RIS_CANDIDATES_LIGHTS; i++){
        float lightWeight;
        LightInfo light = sampleLight(rngState, _sample, surface, lightWeight);
        if(isBlack(light.radiance)){
            continue;
        }


        if(bool(shadow_ray_in_ris) && !isVisisble(light, surface)){
            continue;
        }

        float candidatePdfG = luminance(light.radiance);
        float candidateRISWeight = candidatePdfG * lightWeight;

        totalWeights += candidateRISWeight;
        if(rand(rngState) < (candidateRISWeight / totalWeights)){
            selectedSample = light;
            samplePdfG = candidatePdfG;
        }
    }

    if(totalWeights == 0.0f){
        return false;
    }else {
        lightSampleWeight = (totalWeights / float(RIS_CANDIDATES_LIGHTS)) / samplePdfG;
        return true;
    }

}

Rectangle findRectangle(int instanceId){
    ShapeRef ref;
    for(int i = 0; i < 10; i++){    // TODO add shape ref count to uniforms
        ref = shapeRefs[i];
        if(ref.shape == SHAPE_RECTANGLE && ref.objectId == instanceId){
            break;
        }
        while(i == 9){} // crash shader, we should find a reference
    }
    return rectangles[ref.shapeId];
}

Disk findDisk(int instanceId){
    ShapeRef ref;
    for(int i = 0; i < 10; i++){    // TODO add shape ref count to uniforms
        ref = shapeRefs[i];
        if(ref.shape == SHAPE_DISK && ref.objectId == instanceId){
            break;
        }
        while(i == 9){} // crash shader, we should find a reference
    }
    return disks[ref.shapeId];
}

Sphere findSphere(int instanceId){
    ShapeRef ref;
    for(int i = 0; i < 10; i++){    // TODO add shape ref count to uniforms
        ref = shapeRefs[i];
        if(ref.shape == SHAPE_SPHERE && ref.objectId == instanceId){
            break;
        }
        while(i == 9){} // crash shader, we should find a reference
    }
    return spheres[ref.shapeId];
}

LightInfo sampleLight(inout RngStateType rngState, Sample _sample, Surface surface, out float lightWeight){
    LightInfo lightInfo;
    lightInfo.radiance = vec3(0);
    lightInfo.sx = surface.x;
    lightInfo.sn = surface.sN;
    lightInfo.pdf = 0;
    lightWeight = 0;
    if(numLights <= 0){
        return lightInfo;
    }
    int id = int(rand(rngState) * numLights);
    Light light = lights[id];
    lightInfo.x = light.position;
    lightInfo.n = light.normal;
    lightInfo.area = 1;
    lightInfo.flags = light.flags;
    lightInfo.value = light.value;
    lightInfo.pdf = 0.0;

    if(isPositional(light)){
        vec3 wi = light.position - lightInfo.sx;
        lightInfo.wi = normalize(wi);
        lightInfo.dist = length(wi);
        lightInfo.NdotL = clamp(dot(lightInfo.wi, lightInfo.sn), 0.00001, 1);
        lightInfo.pdf = 1;
        lightInfo.radiance = light.value * fallOff(light, -lightInfo.wi) / (lightInfo.dist * lightInfo.dist);
    }

    if(isDistant(light)){
        lightInfo.wi = -light.normal;
        lightInfo.x = lightInfo.sx + lightInfo.wi  * 2 * worldRadius;
        lightInfo.dist = distance(lightInfo.x, lightInfo.sx);
        lightInfo.NdotL = clamp(dot(lightInfo.wi, lightInfo.sn), 0.00001, 1);
        lightInfo.pdf = 1;
        lightInfo.radiance = light.value;
    }

    if(isArea(light)){
        if(light.shapeType == SHAPE_RECTANGLE){
            Rectangle rect = findRectangle(id);
            float u = rand(rngState);
            float v = rand(rngState);
            lightInfo.x = mix(mix(rect.p0, rect.p1, u), mix(rect.p2, rect.p3, u), v);
            lightInfo.area = area(rect);
        }else if(light.shapeType == SHAPE_DISK){
            vec2 u = randomVec2(rngState);
            vec2 p = concentricSampleDisk(u);
            Disk disk = findDisk(id);
            lightInfo.x = vec3(disk.radius * p.x, disk.height, disk.radius * p.y);
            lightInfo.area = area(disk);
        }else if(light.shapeType == SHAPE_SPHERE){
            Sphere sphere = findSphere(id);
            vec2 u = randomVec2(rngState);
            lightInfo.x = sphere.center + sphere.radius * uniformSampleSphere(u);
            lightInfo.n = normalize(lightInfo.x - sphere.center);
            lightInfo.area = area(sphere);
        }
        else{
            while(true){} // crash shader shape type not implmented
        }

        vec3 wi = lightInfo.x - lightInfo.sx;
        lightInfo.wi = normalize(wi);
        lightInfo.dist = length(wi);
        lightInfo.NdotL = clamp(dot(lightInfo.wi, lightInfo.sn), 0.00001, 1);
        lightInfo.radiance = light.value;
        lightInfo.pdf = (lightInfo.dist * lightInfo.dist)/(lightInfo.NdotL * lightInfo.area);
    }

    if(isInfinite(light)){
        vec2 u = vec2(rand(rngState), rand(rngState));
        float pdf;
        vec2 uv = sampleContinuous2D(pConditionalVFunc, pConditionalVCdf, pMarginal, pMarginalCdf, pMarginalIntegral, u, pdf);
        if(pdf == 0){
            lightInfo.value = vec3(0);
            lightInfo.pdf = 0;
            return lightInfo;
        }
        float sinTheta;
        light.normal = -direction_from_spherical(uv, sinTheta);
        lightInfo.wi = direction_from_spherical(uv, sinTheta);
        lightInfo.x = lightInfo.sx + lightInfo.wi  * 2 * worldRadius;
        lightInfo.dist = distance(lightInfo.x, lightInfo.sx);
        lightInfo.NdotL = clamp(dot(lightInfo.wi, lightInfo.sn), 0.00001, 1);
        lightInfo.pdf = pdf / ( 2 * PI * PI * sinTheta);
        if(sinTheta == 0){
            lightInfo.pdf = 0;
        }
        lightInfo.value = texture(environmentMap, uv).rgb * envMapIntensity;
        lightInfo.radiance = texture(environmentMap, uv).rgb * envMapIntensity;
    }

    lightInfo.radiance = dot(lightInfo.wi, lightInfo.sn) < 0.00001 ? vec3(0) :  lightInfo.radiance;
    lightWeight = 1/lightInfo.pdf;
    return lightInfo;
}

bool isVisisble(LightInfo light, Surface surface){
    occData.isShadowed = true;
    occData.Continue = true;

    occData.ray.origin = offsetRay(surface.x, surface.gN);
    occData.ray.direction = normalize(light.x - surface.x);

    float dist = distance(light.x, surface.x);
    uint flags = gl_RayFlagsTerminateOnFirstHit | gl_RayFlagsSkipClosestHitShader;

    traceRay(topLevelAs, flags, (mask & ~OBJECT_TYPE_LIGHT), 0, 0, 1, occData.ray.origin, 0, occData.ray.direction, dist, 1);


    return !occData.isShadowed;
}

vec3 visibility(LightInfo light, Surface surface){
    occData.isShadowed = true;
    occData.Continue = true;
    occData.transmission = vec3(1);

    occData.ray.origin = offsetRay(surface.x, surface.gN);
    occData.ray.direction = normalize(light.x - surface.x);

    float dist = distance(light.x, surface.x);
    uint flags = gl_RayFlagsNoOpaque;

    while(occData.Continue){
        traceRay(topLevelAs, flags, (mask & ~OBJECT_TYPE_LIGHT), 3, 0, 1, occData.ray.origin, 0, occData.ray.direction, dist, 1);
    }

    return occData.transmission;
}


vec3 evalLightContribution(inout HitData hitData, vec3 wo){
    vec3 lightContribution = vec3(0);
    Surface surface = hitData.surface;

    float lightWeight;
    LightInfo light;
    if(sampleLightRIS(hitData.rngState, hitData._sample, surface, light, lightWeight)){
        if(lightWeight != 0){
            if(bool(shadow_ray_in_ris) || isVisisble(light, surface)){
                vec3 wi = normalize(light.x - surface.x);
                lightContribution = light.radiance * evalBrdf(surface, wo, wi) * lightWeight;
            }
        }
    }

    return lightContribution;
}

vec3 evalLightContributionWithTranmission(inout HitData hitData, vec3 wo, float g){
    vec3 lightContribution = vec3(0);
    Surface surface = hitData.surface;

    float lightWeight;
    LightInfo light;
    if(sampleLightRIS(hitData.rngState, hitData._sample, surface, light, lightWeight)){
        if(lightWeight != 0){
            vec3 wi = normalize(light.x - surface.x);
            float scatteringPdf = HG_p(g, wo, wi);
            vec3 F = vec3(scatteringPdf);
            lightContribution = light.radiance * F * lightWeight * visibility(light, surface);
        }
    }

    return lightContribution;
}

float fallOff(Light light, vec3 pointToLightDir){
    vec3 n = light.normal;
    float cutOff = light.cosWidth;
    float fallOffStart = light.fallOffStart;

    float cos0 = max(0, dot(pointToLightDir, n));
    if(cos0 < cutOff) return 0;
    if(cos0 > fallOffStart) return 1;
    return smoothstep(cutOff, fallOffStart, cos0);
}
#endif