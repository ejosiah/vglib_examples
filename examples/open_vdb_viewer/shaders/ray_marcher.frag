#version 460

#define EPSILON 0.0001
#define PI 3.14159265
#define gID gl_GlobalInvocationID
#define SIZE gl_NumWorkGroups * gl_WorkGroupSize

#include "random.glsl"

struct Bounds{
    vec3 min;
    vec3 max;
};

struct Ray{
    vec3 origin;
    vec3 direction;
    float tNear;
    float tFar;
};

layout(set = 0, binding = 0) uniform CAMERA_UBO {
    mat4 projection;
    mat4 view;
    mat4 inv_projection;
    mat4 inv_view;
} camera;

layout(set = 1, binding = 0) uniform VOLUME_UBO {
    vec3 boxMin;
    vec3 boxMax;
    vec3 lightPosition;
    vec3 color;
    float invMaxDensity;
    float scatteringCoefficient;
    float absorptionCoefficient;
    float extinctionCoefficient;
    int numSamples;
    float coneSpread;
    float g;
    float lightIntensity;
    float time;
    int frame;
    int width;
    int height;
} vd;

const float numLightSamples = 6;

layout(set = 1, binding = 1) uniform sampler3D volume;

layout(location = 0) in struct {
    vec2 uv;
    vec3 ray_direction;
} fs_in;

void swap(inout float a, inout float b){
    float temp = a;
    a = b;
    b = temp;
}

bool test(inout Ray ray, Bounds bounds);

vec4 ray_march(Ray ray, Bounds bounds);

vec3 uvw(vec3 position, Bounds bounds);

layout(location = 0) out vec4 fragColor;

uint rngState;

vec3 randomVec3(inout RngStateType rngState){
    return 2 * vec3(rand(rngState), rand(rngState), rand(rngState))  - 1;
}


void main(){
    gl_FragDepth = 1;
    rngState = initRNG(gl_FragCoord.xy, vec2(vd.width, vd.height), 0);

    Ray ray;
    ray.origin = (camera.inv_view * vec4(0, 0, 0, 1)).xyz;
    ray.direction = normalize(fs_in.ray_direction);

    Bounds bounds = Bounds(vd.boxMin, vd.boxMax);

    fragColor = vec4(mix(vec3(1), vec3(0, 0.3, 0.8), 1 - fs_in.uv.y), 0);
    vec4 volumeColor = vec4(0);
    if(test(ray, bounds)){
        volumeColor = ray_march(ray, bounds);
        volumeColor.rgb /= volumeColor.rgb + 1;
    }

    fragColor.rgb = mix(fragColor.rgb, volumeColor.rgb, volumeColor.a);
    fragColor.a = volumeColor.a;


}

float phaseHG(float cos0, float g){
    float g2 = g * g;

    return (1 - g2)/(4 * PI * pow( 1 + g2 - 2 * g * cos0, 1.5));
}

bool test(inout Ray ray, Bounds bounds){
    float tMin = -10000;
    float tMax = 10000;
    for(int i = 0; i < 3; i++){
        if(abs(ray.direction[i]) < EPSILON){
            if(ray.origin[i] < bounds.min[i] || ray.origin[i] > bounds.max[i]){
                return false;
            }
        }else{
            float ood = 1.0/ray.direction[i];

            float t1 = (bounds.min[i] - ray.origin[i]) * ood;
            float t2 = (bounds.max[i] - ray.origin[i]) * ood;

            if(t1 > t2 ) swap(t1, t2);

            tMin = max(tMin, t1);
            tMax = min(tMax, t2);

            if(tMin > tMax) return false;
        }
    }

    ray.tNear = max(0, tMin);
    ray.tFar = tMax;
    return true;
}

const vec3 noise_kernel[] = {
    vec3(-0.316253, 0.147451, -0.902035),
    vec3(0.208214, 0.114857, -0.669561),
    vec3(-0.398435, -0.105541, -0.722259),
    vec3(0.0849315, -0.644174, 0.0471824),
    vec3(0.470606, 0.99835, 0.498875),
    vec3(-0.207847, -0.176372, -0.847792)
};

float sampleVolumeDensityAlongCone(vec3 sample_pos, vec3 direction){

    float density = 0;
    vec3 light_sample = sample_pos;
    for(int i = 0; i < numLightSamples; i++){
        light_sample += vd.coneSpread * randomVec3(rngState) * float(i);
        density += texture(volume, light_sample).r;
    }

    return density;
}

float sampleLightEnergy(vec3 sample_pos, vec3 view_direction, vec3 light_direction, float voxel){
    if(voxel <= 0) return 1.0;

    float d = sampleVolumeDensityAlongCone(sample_pos, view_direction);
    float cos0 = max(0, dot(-view_direction, light_direction));
    float phase = phaseHG(cos0, vd.g);

    return 2.0 * exp(-d) * (1 - exp(-2.0 * d)) * phase;
}

vec4 ray_march(Ray ray, Bounds bounds){

    vec3 diagonal = bounds.max - bounds.min;
    vec3 step_size = diagonal/float(vd.numSamples);
    vec3 step_dir = ray.direction * step_size;

    vec4 color = vec4(0);
    vec3 entryPoint = vec3(0);
    vec3 position = ray.origin + ray.direction * ray.tNear;
    bool entryPointFound = false;
    for(int i = 0; i < vd.numSamples; i++){
        if(distance(ray.origin, position) > ray.tFar) break;
        if(color.a > 0.99) break;

        vec3 sample_pos = uvw(position, bounds);
        float voxel = texture(volume, sample_pos).r;
        vec3 lightDirection = normalize(vd.lightPosition - position);
        vec3 energy = vd.lightIntensity * sampleLightEnergy(sample_pos, ray.direction, lightDirection, voxel) * vd.color;


        if(!entryPointFound){
            if(voxel != 0){
                entryPointFound = true;
                 entryPoint = position;
            }
        }

        float prev_alpha = voxel - (voxel * color.a);
        color.rgb = prev_alpha * voxel * energy + color.rgb;
        color.a += prev_alpha;

        position += step_dir + randomVec3(rngState) * 0.2;
    }


    if(entryPointFound){
        vec4 clipPoint = camera.projection * camera.view * vec4(entryPoint, 1);
        clipPoint /= clipPoint.w;
        gl_FragDepth = clipPoint.z;
    }

    return color;
}

vec3 uvw(vec3 position, Bounds bounds){
    vec3 t = (position - bounds.min)/(bounds.max - bounds.min);

    return mix(vec3(0), vec3(1), t);
}