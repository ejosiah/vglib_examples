#ifndef INTRO_VOLUME_RENDERING_SHARED_GLSL
#define INTRO_VOLUME_RENDERING_SHARED_GLSL

#include "random.glsl"
#include "sampling.glsl"
#include "raytracing_implicits/implicits.glsl"
#include "perlin_noise.glsl"

#define noise_texture global_textures_array[blue_noise_tex_id]
#define volume_texture global_textures_3d[volume_tex_id]
#define volume_emission global_textures_3d[volume_emission_tex_id]

layout(set = 1, binding = 0, scalar) uniform Globals {
    mat4 projection;
    mat4 view;
    mat4 worldToTextureSpace;
    vec4 bmin;
    vec4 bmax;
    vec4 scatter;
    vec4 absorption;
    vec2 resolution;
    float near;
    float far;
    int density_method;
    float frequency;
    float falloff;
    float bias;
    float max_density;
    float max_intensity;
    float intensity_zero;
    uint frame;
    uint color_tex_id;
    uint depth_tex_id;
    uint blue_noise_tex_id;
    uint volume_tex_id;
    uint volume_emission_tex_id;
};

layout(set = 0, binding = 10) uniform sampler2D global_textures[];
layout(set = 0, binding = 10) uniform sampler2DArray global_textures_array[];
layout(set = 0, binding = 10) uniform sampler3D global_textures_3d[];

vec3 sigma_a = absorption.xyz * absorption.w;
vec3 sigma_s = scatter.xyz * scatter.w;
vec3 sigma_t = sigma_a + sigma_s;

const float epsilon = 1e-3;
const float rr = 2;
uint seed = 0;
RngStateType rngState;

float luminance(vec3 rgb){
    return dot(rgb, vec3(0.2126f, 0.7152f, 0.0722f));
}

vec3 fire_ramp(float t) {
    if (t < 0.33) {
        return mix(vec3(120.0/255.0, 0.0, 0.0),
        vec3(1.0, 0.24, 0.0),
        t / 0.33);
    } else if (t < 0.66) {
        return mix(vec3(1.0, 0.24, 0.0),
        vec3(1.0, 0.71, 0.20),
        (t - 0.33) / 0.33);
    } else {
        return mix(vec3(1.0, 0.71, 0.20),
        vec3(1.0, 0.94, 0.78),
        (t - 0.66) / 0.34);
    }
}

vec3 fire_ramp2(float t){
    // Clamp to [0,1]
    t = clamp(t, 0.0, 1.0);

    if (t < 0.25) {
        return mix(vec3(0.0), vec3(120.0/255.0, 0.0, 0.0), t / 0.25);
    } else if (t < 0.5) {
        return mix(vec3(120.0/255.0, 0.0, 0.0),
        vec3(1.0, 60.0/255.0, 0.0),
        (t - 0.25) / 0.25);
    } else if (t < 0.75) {
        return mix(vec3(1.0, 60.0/255.0, 0.0),
        vec3(1.0, 180.0/255.0, 50.0/255.0),
        (t - 0.5) / 0.25);
    } else {
        return mix(vec3(1.0, 180.0/255.0, 50.0/255.0),
        vec3(1.0, 240.0/255.0, 200.0/255.0),
        (t - 0.75) / 0.25);
    }
}



#endif // INTRO_VOLUME_RENDERING_SHARED_GLSL