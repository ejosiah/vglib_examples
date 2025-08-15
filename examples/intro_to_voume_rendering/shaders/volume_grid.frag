#version 460 core
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "random.glsl"
#include "sampling.glsl"
#include "raytracing_implicits/implicits.glsl"
#include "perlin_noise.glsl"

#define noise_texture global_textures_array[blue_noise_tex_id]
#define volume_texture global_textures_3d[volume_tex_id]

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
layout(set = 0, binding = 10) uniform sampler3D global_textures_3d[];

layout(location = 0) in Ray ray;
layout(location = 2) in mat4 model;

layout(location = 0) out vec4 fragColor;


Sphere sphere = Sphere((model * vec4(0, 0, 0, 1)).xyz, 0.8);

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

float updateDepthBuffer(vec3 p);
float phase(float g, float cos_theta);
float sampleDensity(vec3 p);
float sampleNoise(inout uint seed);

void main(){
    rngState = initRNG(gl_FragCoord.xy, resolution, frame);

    Ray cam_ray = Ray(ray.origin, normalize(ray.direction));
    Box box = Box(bmin.xyz, bmax.xyz);

    vec2 cam_isect;
    if(!box_ray_test(box, cam_ray, cam_isect)) discard;

    float transparancy = 1;
    vec3 result = vec3(0);
    vec3 V = normalize(-cam_ray.direction);
    vec3 L = normalize(lightDirection);
    float VDotL = dot(V, L);

    float t0 = cam_isect.x;
    float t1 = cam_isect.y;

    //    float u = rand(rngState);        // in [0,1)
    float u = sampleNoise(seed);        // in [0,1)
    float t = t0 + u * step_size;

    bool firstHit = true;
    for(; t < t1; t += step_size) {
        vec3 sample_pos = cam_ray.origin + t * cam_ray.direction;


        float density = sampleDensity(sample_pos);
        float sample_attenuation = exp(-step_size * density * sigma_t);
        transparancy *= sample_attenuation;

        Ray rl = Ray(sample_pos, lightDirection);
        vec2 v_isect;
        if(density > 0 && box_ray_test(box, rl, v_isect)) {
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

        if(firstHit && density > 0) {
            updateDepthBuffer(sample_pos);
            firstHit = false;
        }

        if(transparancy < epsilon) {
            if(rand(rngState) > 1/rr) break;
            else transparancy *= rr;
        }
    }

    fragColor = vec4(result, transparancy);

}

float updateDepthBuffer(vec3 p) {
    vec4 clipPos = projection * view * vec4(p, 1);
    clipPos.xyz /= clipPos.w;
    gl_FragDepth = clipPos.z;

    return gl_FragDepth;
}

float phase(float g, float cos_theta) {
    float denom = 1 + g * g - 2 * g * cos_theta;
    return 1 / (4 * 3.14159265358979323846) * (1 - g * g) / (denom * sqrt(denom));
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

float sampleDensity(vec3 pos) {
    vec4 p = worldToTextureSpace * vec4(pos, 1);
    return texture(volume_texture, p.xyz).r;
}