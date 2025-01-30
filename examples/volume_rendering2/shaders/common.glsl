#ifndef VOLUME_RENDERING_COMMONG_GLSL
#define VOLUME_RENDERING_COMMONG_GLSL

#include "random.glsl"

#define EPSILION_VEC3 vec3(0.0001)
#define M_PI 3.1415926535897932384626433832795

#define DENSITY_TEXTURE0 (global_textures[0])
#define DENSITY_TEXTURE (global_textures[scene.currentFrame])
#define EMISSION_TEXTURE (global_textures[scene.texturePoolSize + scene.currentFrame])

uint rngState;

struct Scene {
    vec3 lightDirection;
    vec3 lightColor;
    vec3 scattering;
    vec3 absorption;
    vec3 extinction;
    vec3 cameraPosition;
    float primaryStepSize;
    float shadowStepSize;
    float gain;
    float cutoff;
    float isoLevel;
    int shadow;
    float lightConeSpread;
    int currentFrame;
    int texturePoolSize;
    int numSteps;
    float asymmetric_factor;
    float invMaxDensity;
    float time;
    int width;
    int height;
};

struct VolumeInfo {
    mat4 worldToVoxelTransform;
    mat4 voxelToWorldTransform;
    vec3 bmin;
    vec3 bmax;
};

struct TimeSpan {
    float t0;
    float t1;
};

float length(TimeSpan span) {
    return span.t1 - span.t0;
}


layout(set = 0, binding = 10) uniform sampler3D global_textures[];

layout(set = 0, binding = 0, scalar) buffer ssboScene {
    Scene scene;
};

layout(set = 1, binding = 0, scalar) buffer Info {
    VolumeInfo info;
};

layout(push_constant) uniform  Constants {
    mat4 model;
    mat4 view;
    mat4 proj;
};

float cutoff = scene.cutoff;

vec3 light_dir = normalize(scene.lightDirection);
vec3 light_color = scene.lightColor;
float g = scene.asymmetric_factor;


bool test(vec3 o, vec3 rd, vec3 bmin, vec3 bmax, out TimeSpan span) {
    float tmin = -1e10;
    float tmax = 1e10;

    for(int i = 0; i < 3; ++i) {
        if(abs(rd[i]) < 1e-6){
            // ray is parallel to slab. No hit if origin not within slab
            if(o[i] < bmin[i] || o[i] > bmax[i]) return false;
        }else {
            float invRd = 1.0/rd[i];
            float t1 = (bmin[i] - o[i]) * invRd;
            float t2 = (bmax[i] - o[i]) * invRd;

            if(t1 > t2) {
                float temp = t1;
                t1 = t2;
                t2 = temp;
            }
            tmin = max(tmin, t1);
            tmax = min(tmax, t2);
            if(tmin > tmax) return false;
        }
    }
    span.t0 = max(0, tmin);
    span.t1 = tmax;
    return true;
}

bool outOfBounds(vec3 pos) {
    bvec3 near = lessThan(pos, vec3(0));
    bvec3 far = greaterThanEqual(pos, vec3(1));

    return any(near) || any(far);
}

float F(vec3 x) {
    return texture(DENSITY_TEXTURE, x).r;
}

vec3 bisection(vec3 left, vec3 right, float iso) {
    for(int i = 0; i < 4; ++i) {
        vec3 midpoint = (right + left) * 0.5;
        float cM = texture(DENSITY_TEXTURE, midpoint).x;
        if(cM < iso){
            left = midpoint;
        }else {
            right = midpoint;
        }
    }
    return (right + left) * 0.5;
}

vec3 computeNormal(vec3 p0, vec3 p1, float isoValue) {

    vec3 p = bisection(p0, p1, isoValue);

    vec3 d = 1/vec3(textureSize(DENSITY_TEXTURE, 0));
    float dx = (F(vec3(p.x + d.x, p.yz)) - F(vec3(p.x - d.x, p.yz))) * 0.5 ;
    float dy = (F(vec3(p.x, p.y + d.y, p.z)) - F(vec3(p.x, p.y - d.y, d.z))) * 0.5 ;
    float dz = (F(vec3(p.xy, p.z + d.z)) - F(vec3(p.xy, p.z - d.z))) * 0.5;

    return normalize(vec3(dx, dy, dz));
}

vec3 worldToVoxel(vec3 pos, vec3 direction) {
    return (info.worldToVoxelTransform * vec4(pos, 1)).xyz + sign(direction) * 0.5/vec3(textureSize(DENSITY_TEXTURE, 0));
}


vec3 worldToVoxel(vec3 pos) {
    return (info.worldToVoxelTransform * vec4(pos, 1)).xyz;
}

vec3 voxelToWorld(vec3 pos) {
    return (info.voxelToWorldTransform * vec4(pos, 1)).xyz;
}

vec3 directionWorldToVoxel(vec3 dir) {
    vec3 start = worldToVoxel(dir);
    vec3 target = worldToVoxel(dir + dir);
    return normalize(target - start);
}

float sampleDensity(vec3 pos) {
    return texture(DENSITY_TEXTURE, pos).r;
}

float sampleEmission(vec3 pos) {
    return texture(EMISSION_TEXTURE, pos).r;
}

float sampleDensityDT(vec3 rO, vec3 rD, TimeSpan ts, out float t) {
    const float scale = length(ts);
    const float tMax = ts.t1;
    t = ts.t0;
    while(t < tMax){
        t -= log(1 - rand(rngState)) * scale * scene.invMaxDensity;
        if(t >= tMax) {
            break;
        }
        vec3 pos = rO + rD * t;
        float density = sampleDensity(pos);
        if(density * scene.invMaxDensity > rand(rngState)){
            return density;
        }
    }
    return 0;
}

void writeDepthValue(vec3 pos) {
    vec4 clipPos = proj * view * model * vec4(pos, 1);
    clipPos /= clipPos.w;
    gl_FragDepth = clipPos.z;
}

vec3 volumeDim() {
    return vec3(textureSize(DENSITY_TEXTURE, 0));
}

float maxVolumeDim() {
    vec3 dim = volumeDim();
    return max(dim.x, max(dim.y, dim.z));
}

float computer_step_size() {
    vec3 inv_dim = 1/(info.bmax - info.bmin);
    return min(inv_dim.x, min(inv_dim.y, inv_dim.z));
}

float getPerticipatingMedia(vec3 pos, out vec3 sigma_s, out vec3 sigma_a, out vec3 sigma_t) {
    float density = sampleDensity(pos);
    sigma_a = scene.absorption * density;
    sigma_s = scene.scattering * density;
    sigma_t = max(EPSILION_VEC3, sigma_s + sigma_a);

    return density;
}

float phaseHG(float g, float cos0) {
    return 1 / (4 * M_PI) * (1 - g * g) / pow(1 + g * g - 2 * g * cos0, 1.5);
}

vec3 compute_light_attenuation(vec3 position, float primary_step_size) {
    vec3 lo =  position;
    vec3 ld = directionWorldToVoxel(light_dir);

    TimeSpan ts;
    if(!test(lo, ld, vec3(0), vec3(1), ts)) return vec3(1);

    const int num_steps = int(ceil(ts.t1/primary_step_size));
    const float stride = ts.t1/num_steps;

    vec3 start = lo;
    ld *= stride;

    vec3 tau = vec3(0);
    vec3 sigma_a, sigma_s, sigma_t;
    for(int t = 0; t < num_steps; ++t) {
        vec3 sample_position  = start + ld * t;

        float density = getPerticipatingMedia(sample_position, sigma_s, sigma_a, sigma_t);
        if (density < cutoff ) continue;

        tau += sigma_t;
    }

    return exp(-tau * stride);
}

#endif // VOLUME_RENDERING_COMMONG_GLSL