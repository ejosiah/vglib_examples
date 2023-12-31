#version 460

#define EPSILON 0.0001
#define PI 3.14159265
#define gID gl_GlobalInvocationID
#define SIZE gl_NumWorkGroups * gl_WorkGroupSize
#define MIN_BOUNCES 3

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

float phaseHG(float cos0, float g);

float HG_p(float g, vec3 wo, vec3 wi);

float HG_sample_P(float g, vec3 wo, out vec3 wi, vec2 u);

float luminance(vec3 rgb);

vec3 uvw(vec3 position, Bounds bounds);

float remap(float x, float a, float b, float c, float d);

bool test(inout Ray ray, Bounds bounds);

vec2 randomVec2(inout RngStateType rngState);

bool sampleVolume(inout Ray ray, Bounds bounds, inout RngStateType rngState, out vec3 transimttance);

vec3 calcTransmittance(Ray ray, Bounds bounds, inout RngStateType rngState);

Ray nextRay(Ray ray, float g, inout RngStateType rngState);

vec3 sphericalDirection(float sin0, float cos0, float phi, vec3 x, vec3 y, vec3 z);

void othonormalBasis(out vec3 tangent, out vec3 binormal, inout vec3 normal);

vec3 sampleLight(Ray ray, Bounds bounds, float g, inout RngStateType rngState);

void swap(inout float a, inout float b);

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

layout(set = 1, binding = 1) uniform sampler3D volume;

layout(set = 2, binding = 0) uniform SCENE_UBO {
    int width;
    int height;
    int frame;
    float timeDelta;
    float elapsedTime;
    int numSamples;
    int currentSample;
    int bounces;
} scene;

layout(set = 2, binding = 1) uniform sampler2D previousFrameTex;

uint rngState;
const int bounces = 100;

layout(location = 0) in struct {
    vec2 uv;
    vec3 ray_direction;
} fs_in;

layout(location = 0) out vec4 fragColor;


void main(){
  //  gl_FragDepth = 1;

    vec3 backgroundColor = mix(vec3(1), vec3(0, 0.3, 0.8), 1 - fs_in.uv.y);
    if(vd.invMaxDensity <= 0 || scene.currentSample >= scene.numSamples){
        fragColor.rgb = backgroundColor;
        return;
    }

    rngState = initRNG(gl_FragCoord.xy, vec2(vd.width, vd.height), 0);

    Ray ray;
    ray.origin = (camera.inv_view * vec4(0, 0, 0, 1)).xyz;
    ray.direction = normalize(fs_in.ray_direction);

    Bounds bounds = Bounds(vd.boxMin, vd.boxMax);

    const float g = vd.g;
    vec3 Lo = vec3(0);
    vec3 throughput = vec3(1);

    for(int bounce = 0; bounce < bounces; bounce++){
        if(!test(ray, bounds)){
            Lo += throughput * backgroundColor;
            break;
        }

        vec3 transmittance;
        bool hitCloud = sampleVolume(ray, bounds, rngState, transmittance);
        throughput *= transmittance;

        bool rayConsumed = all(equal(throughput, vec3(0)));
        if(rayConsumed){
            break;
        }

        if(hitCloud){
            Lo += throughput * sampleLight(ray, bounds, g, rngState) * vd.color;
            ray = nextRay(ray, g, rngState);
        }else{
            Lo += throughput * backgroundColor;
            break;
        }

        if(bounces >= bounces - 1) break;

        // possible termination with Russian roulette
        if(bounces > MIN_BOUNCES){
            float q = min(.05, 1 - luminance(throughput));
            if(rand(rngState) < q){
                break;
            }
            throughput /= 1 - q;
        }
    }
    vec3 previousLo = texture(previousFrameTex, fs_in.uv).rgb;
    float t = 1/float(scene.currentSample + 1);

    fragColor.rgb = mix(previousLo, Lo, t);

}

vec3 sampleLight(Ray ray, Bounds bounds, float g, inout RngStateType rngState){
    vec3 p = ray.origin + ray.direction * ray.tNear;
    vec3 wi = normalize(p - vd.lightPosition);
    float scatteringPdf = HG_p(g, -ray.direction, wi);
    vec3 F = vec3(scatteringPdf);
    return vd.lightIntensity * F * calcTransmittance(ray, bounds, rngState);
}

Ray nextRay(Ray ray, float g, inout RngStateType rngState){
    vec3 wo = -ray.direction;
    vec3 wi;
    vec2 u = randomVec2(rngState);
    HG_sample_P(g, wo, wi, u);

    return Ray(ray.origin + ray.direction * ray.tNear, wi, 0, 10000);
}

bool sampleVolume(inout Ray ray, Bounds bounds, inout RngStateType rngState, out vec3 transimttance){
    const float scale = ray.tFar - ray.tNear;
    const float tMax = ray.tFar;

    while(ray.tNear < tMax){
        ray.tNear -= log(1 - rand(rngState)) * scale * vd.invMaxDensity / vd.extinctionCoefficient;
        if(ray.tNear >= tMax) {
            break;
        }
        vec3 pos = ray.origin + ray.direction * ray.tNear;
        vec3 samplePos = uvw(pos, bounds);
        float density = texture(volume, samplePos).r;
        if(density * vd.invMaxDensity > rand(rngState)){
            transimttance = vec3(vd.scatteringCoefficient/vd.extinctionCoefficient);
            return true;
        }
    }
    transimttance = vec3(1);
    return false;
}

vec3 calcTransmittance(Ray ray, Bounds bounds, inout RngStateType rngState){
    const float scale = ray.tFar - ray.tNear;
    const float tMax = ray.tFar;
    float t = ray.tNear;
    float Tr = 1;

    while(t < tMax){
        t -= log(1 - rand(rngState)) * scale * vd.invMaxDensity / vd.extinctionCoefficient;
        if(t >= tMax){
            break;
        }
        vec3 pos = ray.origin + ray.direction * ray.tNear;
        vec3 samplePos = uvw(pos, bounds);
        float density = texture(volume, samplePos).r;
        Tr *= 1 - max(0, density * vd.invMaxDensity);
    }
    return vec3(Tr);
}

float remap(float x, float a, float b, float c, float d){
    return mix(c, d, (x - a)/(b - a));
}

float luminance(vec3 rgb){
    return dot(rgb, vec3(0.2126f, 0.7152f, 0.0722f));
}

float phaseHG(float cos0, float g){
    float denom = 1 + g * g + 2 * g * cos0;
    return 0.25 * PI * (1 - g * g) / (denom * sqrt(denom));
}

float HG_p(float g, vec3 wo, vec3 wi){
    return phaseHG(dot(wo, wi), g);
}

float HG_sample_P(float g, vec3 wo, out vec3 wi, vec2 u){
    float cos0;
    if(abs(g) < 0.001){
        cos0 = 1 - 2 * u.x;
    }else {
        float sqrTerm = (1 - g * g) / (1 - g + 2 * g * u.x);
        cos0 = (1 + g * g - sqrTerm * sqrTerm) / (2 * g);
    }

    float sin0 = sqrt(max(0, 1 - cos0 * cos0));
    float phi = 2 * PI * u.y;
    vec3 v1, v2;
    othonormalBasis(v1, v2, wo);
    wi = sphericalDirection(sin0, cos0, phi, v1, v2, -wo);
    return phaseHG(-cos0, g);
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

void othonormalBasis(out vec3 tangent, out vec3 binormal, inout vec3 normal){
    normal = normalize(normal);
    vec3 a;
    if(abs(normal.x) > 0.9){
        a = vec3(0, 1, 0);
    }else {
        a = vec3(1, 0, 0);
    }
    binormal = normalize(cross(normal, a));
    tangent = cross(normal, binormal);
}

vec3 sphericalDirection(float sin0, float cos0, float phi, vec3 x, vec3 y, vec3 z){
    return sin0 * cos(phi) * x + sin0 * sin(phi) * y + cos0 * z;
}

void swap(inout float a, inout float b){
    float temp = a;
    a = b;
    b = temp;
}

vec3 uvw(vec3 position, Bounds bounds){
    vec3 t = (position - bounds.min)/(bounds.max - bounds.min);

    return mix(vec3(0), vec3(1), t);
}

vec2 randomVec2(inout RngStateType rngState){
    return vec2(rand(rngState), rand(rngState));
}