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
    vec3 wrO = scene.cameraPosition;
    vec3 wrd = normalize(fs_in.direction);

    vec3 rO = worldToVoxel(wrO);
    vec3 rd = directionWorldToVoxel(wrd);

    TimeSpan pTS;
    float phase_debug = 0;
    if(!test(rO, rd, vec3(0), vec3(1), pTS)) discard;

    vec3 entry_position = rO + rd * pTS.t0;

    float step_size = scene.primaryStepSize/float(scene.numSteps);
    const int num_steps = int(ceil(length(pTS)/step_size));
    const float stride = length(pTS)/num_steps;

    vec3 transmission = vec3(1);
    vec3 inScattering = vec3(0);
    vec3 sigma_a, sigma_s, sigma_t;
    const float cos0 = dot(-wrd, light_dir);

    vec3 start = entry_position;
    rd = rd * stride;

    float in_scatter_probablity = phaseHG(g, cos0);
    phase_debug += in_scatter_probablity;
    vec3 volume_entry_point;
    bool first_hit = false;

    for(int t = 0; t < num_steps; ++t) {
        vec3 sample_position  = start + rd * t;

        float density = getPerticipatingMedia(sample_position, sigma_s, sigma_a, sigma_t);
        if (density < cutoff ) continue;

        if(!first_hit) {
            volume_entry_point = sample_position;
            first_hit = true;
        }

        vec3 sample_transmission = exp(-sigma_t * stride);
        transmission *= sample_transmission;
        vec3 light_attenuation = compute_light_attenuation(sample_position, step_size);
        
        vec3 light_intensity = light_color * light_attenuation;
        inScattering += transmission * sigma_s * in_scatter_probablity * light_intensity * stride;

    }

    vec3 world_position = voxelToWorld(volume_entry_point);

    float alpha = 1 - dot(vec3(1), transmission/3);
    fragColor = vec4(inScattering, alpha);
    writeDepthValue(world_position);

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