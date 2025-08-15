#ifndef INTRO_VOLUME_RENDERING_SHARED_GLSL
#define INTRO_VOLUME_RENDERING_SHARED_GLSL

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
    vec4 scatter;
    vec4 absorption;
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

#endif // INTRO_VOLUME_RENDERING_SHARED_GLSL