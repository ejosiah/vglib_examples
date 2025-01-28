#version 460
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "common.glsl"


layout(location = 0) in struct {
    vec3 direction;
} fs_in;

layout(location = 0) out vec4 fragColor;

float cutoff = scene.cutoff;

vec3 compute_light_attenuation(vec3 position, float primary_step_size);

float phaseHG(float g, float cos0);

vec3 light_dir = normalize(scene.lightDirection);
vec3 light_color = scene.lightColor;
float g = scene.asymmetric_factor;

void main() {
    vec3 rO = scene.cameraPosition;
    vec3 rd = normalize(fs_in.direction);

    TimeSpan pTS;
    if(!test(rO, rd, info.bmin, info.bmax, pTS)) discard;

    vec3 world_position = rO + rd * pTS.t0;

    float step_size = scene.primaryStepSize/float(scene.numSteps);
    const int num_steps = int(ceil(length(pTS)/step_size));
//    const int num_steps = int(1/step_size);
    const float stride = length(pTS)/num_steps;
//    const float stride = 1/num_steps;

    vec3 transmission = vec3(1);
    vec3 inScattering = vec3(0);
    vec3 sigma_a, sigma_s, sigma_t;

    vec3 start = worldToVoxel(world_position, rd);
    vec3 target = worldToVoxel(world_position + rd, rd);

    rd = normalize(target - start) * stride;

    const float cos0 = dot(-rd, light_dir);
    float in_scatter_probablity = phaseHG(g, cos0);

    for(int t = 0; t < num_steps; ++t) {
        vec3 sample_position  = start + rd * t;

        float density = getPerticipatingMedia(sample_position, sigma_s, sigma_a, sigma_t);
        if (density < cutoff ) continue;

        vec3 sample_transmission = exp(-sigma_t * stride);
        transmission *= sample_transmission;
        vec3 light_attenuation = compute_light_attenuation(sample_position, step_size);
        
        vec3 light_intensity = light_color * light_attenuation;
        inScattering += transmission * sigma_s * in_scatter_probablity * light_intensity * stride;

    }

    float alpha = 1 - dot(vec3(1), transmission/3);
    fragColor = vec4(inScattering, alpha);
    writeDepthValue(world_position);

}

float phaseHG(float g, float cos0) {
    return 1 / (4 * M_PI) * (1 - g * g) / pow(1 + g * g - 2 * g * cos0, 1.5);
}

vec3 compute_light_attenuation(vec3 position, float primary_step_size) {
    vec3 lo =  voxelToWorld(position);
    vec3 ld = light_dir;

    TimeSpan ts;
    if(!test(lo, ld, info.bmin, info.bmax, ts)) return vec3(1);

    const int num_steps = int(ceil(ts.t1/primary_step_size));
    const float stride = ts.t1/num_steps;

    vec3 start = worldToVoxel(lo, ld);
    vec3 target = worldToVoxel(start + ld, ld);
    ld = normalize(target - start) * stride;

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