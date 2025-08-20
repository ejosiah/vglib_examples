#version 460 core
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "shared.glsl"

layout(location = 0) in Ray ray;
layout(location = 2) in mat4 model;

layout(location = 0) out vec4 fragColor;


Sphere sphere = Sphere((model * vec4(0, 0, 0, 1)).xyz, 0.8);

const vec3 lightColor = vec3(20);
vec3 lightDirection = vec3(-0.315798, 0.719361, 0.618702);
const vec3 background_color = vec3(0.572, 0.772, 0.921);
const float g = 0;
float step_size = 0.1;
float light_step_size = 0.2;


float updateDepthBuffer(vec3 p);
float phase(float g, float cos_theta);
float sampleDensity(vec3 p);
float sampleNoise(inout uint seed);
vec3 sampleEmission(vec3 pos);

void main(){
    rngState = initRNG(gl_FragCoord.xy, resolution, frame);

    Ray cam_ray;
    cam_ray.origin = (worldToTextureSpace * vec4(ray.origin, 1)).xyz;
    cam_ray.direction = (worldToTextureSpace * vec4(ray.direction, 0)).xyz;
    lightDirection = (worldToTextureSpace * vec4(lightDirection, 0)).xyz;

    Box box = Box(vec3(0), vec3(1));

    vec2 cam_isect;
    if(!box_ray_test(box, cam_ray, cam_isect)) discard;

    vec3 transparancy = vec3(1);
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
        vec3 sample_attenuation = exp(-step_size * density * sigma_t);
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
            vec3 light_ray_att = exp(-tau * light_step_size * sigma_t);

            result += lightColor * light_ray_att * phase(g, VDotL) * sigma_s * transparancy * step_size * density;
        }

        result += sampleEmission(sample_pos) * sigma_a * density * transparancy;

        if(firstHit && density > 0) {
            vec4 p = inverse(worldToTextureSpace) * vec4(sample_pos, 1);
            updateDepthBuffer(p.xyz);
            firstHit = false;
        }

        if(luminance(transparancy) < epsilon) {
            if(rand(rngState) > 1/rr) break;
            else transparancy *= rr;
        }
    }

    fragColor = vec4(result, luminance(transparancy));

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
    return texture(volume_texture, pos).r;
}

vec3 sampleEmission(vec3 pos) {
    if(volume_emission_tex_id == ~0u) return vec3(0);

    float intensity = texture(volume_emission, pos).r;

    float t =  smoothstep( intensity_zero, max_intensity, intensity);
    return fire_ramp2(t);
}