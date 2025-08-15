#version 460 core
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "random.glsl"
#include "sampling.glsl"
#include "raytracing_implicits/implicits.glsl"
#include "perlin_noise.glsl"

#define noise_texture global_textures_array[blue_noise_tex_id]

layout(set = 1, binding = 0, scalar) uniform Globals {
    mat4 projection;
    mat4 view;
    mat4 worldToTextureSpace;
    vec4 bmin;
    vec4 bmax;
    vec2 resolution;
    float near;
    float far;
    int density_method;
    float frequency;
    float falloff;
    float bias;
    uint frame;
    uint color_tex_id;
    uint depth_tex_id;
    uint blue_noise_tex_id;
    uint volume_tex_id;
};

layout(set = 0, binding = 10) uniform sampler2D global_textures[];
layout(set = 0, binding = 10) uniform sampler2DArray global_textures_array[];

layout(location = 0) in Ray ray;
layout(location = 2) in mat4 model;

layout(location = 0) out vec4 fragColor;


Sphere sphere = Sphere((model * vec4(0, 0, 0, 1)).xyz, 0.8);
vec2 isect;

const vec3 lightColor = vec3(20);
const vec3 lightDirection = vec3(-0.315798, 0.719361, 0.618702);
const vec3 background_color = vec3(0.572, 0.772, 0.921);
const float g = 0;
float step_size = 0.1;
float light_step_size = 0.2;
const float sigma_a = 10;
const float sigma_s = 5;
const float sigma_t = sigma_a + sigma_s;
const float epsilon = 1e-3;
const float rr = 2;
uint seed = 0;
RngStateType rngState;

void updateDepthBuffer(vec3 p);
float phase(float g, float cos_theta);
float sampleDensity(vec3 p);
float sampleNoise(inout uint seed);

void main(){
    rngState = initRNG(gl_FragCoord.xy, resolution, frame);

    Ray r = ray;
    Sphere s = sphere;
    if(!sphere_ray_test(s, r, isect)) {
        discard;
    }

    vec3 hitPoint = r.origin + r.direction * isect.x;
    updateDepthBuffer(hitPoint);

    float transparancy = 1;
    vec3 result = vec3(0);
    vec3 V = normalize(-r.direction);
    vec3 L = normalize(lightDirection);
    float VDotL = dot(V, L);

    float t0 = isect.x;
    float t1 = isect.y;

//    float u = rand(rngState);        // in [0,1)
    float u = sampleNoise(seed);        // in [0,1)
    float t = t0 + u * step_size;

    for(; t < t1; t += step_size) {
        vec3 sample_pos = ray.origin + t * ray.direction;

        float density = sampleDensity(sample_pos);
        float sample_attenuation = exp(-step_size * density * sigma_t);
        transparancy *= sample_attenuation;

        Ray rl = Ray(sample_pos, lightDirection);
        vec2 v_isect;
        if(density > 0 && sphere_ray_test(s, rl, v_isect) && v_isect.x == 0) {
            const float t1_light = v_isect.y;
//            float t_light = rand(rngState) * light_step_size;
            float t_light = sampleNoise(seed) * light_step_size;

            float tau = 0;
            for(; t_light < t1_light;  t_light += light_step_size) {
                vec3 light_sample_pos = rl.origin + t_light * rl.direction;
                tau += sampleDensity(light_sample_pos);
            }
            float light_ray_att = exp(-tau * light_step_size * sigma_t);

            result += lightColor * light_ray_att * phase(g, VDotL) * sigma_s * transparancy * step_size * density;
        }

        if(transparancy < epsilon) {
            if(rand(rngState) > 1/rr) break;
            else transparancy *= rr;
        }
    }

    fragColor = vec4(result, transparancy);

}

void updateDepthBuffer(vec3 p) {
    vec4 clipPos = projection * view * vec4(p, 1);
    clipPos.xyz /= clipPos.w;
    gl_FragDepth = clipPos.z;
}

float phase(float g, float cos_theta) {
    float denom = 1 + g * g - 2 * g * cos_theta;
    return 1 / (4 * 3.14159265358979323846) * (1 - g * g) / (denom * sqrt(denom));
}

float eval_density(vec3 sample_pos, vec3 sphere_center, float sphere_radius){
    vec3 vp = sample_pos - sphere_center;
    float dist = min(1.f, length(vp) / sphere_radius);
    float falloff = smoothstep(0.8, 1, dist); // smooth transition from 0 to 1 as distance goes from 0.1 to 1
    return (1 - falloff);
}

float fbm(vec3 p) {
    vec3 vp = p - sphere.center;
    vp *= frequency; // scale the initial point value if necessary
    uint numOctaves = 5; // number of layers
    float lacunarity = 2.f; // gap between successive frequencies
    float H = 0.4; // fractal increment parameter
    float value = 0; // result of the fBm (use this for our density)
    for (uint i = 0; i < numOctaves; ++i) {
        value += perlin(vp) * pow(lacunarity, -H * i);
        vp *= lacunarity;
    }
    return max(0, value) * (1 - falloff);
}


float sampleDensity(vec3 p) {
    float noise = perlin(p * frequency);
    float density;
    switch(density_method){
        case 0:
            density = noise * 0.5 + 0.5;
            break;
        case 1:
            density = max(0, noise) * (1 - falloff);
            break;
        case 2:
            density = abs(noise) * (1 - falloff);
            break;
        case 3:
            density = fbm(p);
            break;
        case 4:
            density = eval_density(p, sphere.center, sphere.radius);
            break;
        default:
            density = 1;
    }
    float e = (bias - 1)/(-bias - 1);
    return pow(density, e);
}

float sampleNoise(inout uint seed) {
    vec2 uv = gl_FragCoord.xy/resolution;

    vec3 tSize = textureSize(noise_texture, 0);
    float layer = mod(frame + seed, tSize.z);
    vec2 numTiles = resolution/tSize.xy;
    vec2 tileUV = fract(uv * numTiles);
    ++seed;

    return texture(noise_texture, vec3(tileUV, layer)).x;
}