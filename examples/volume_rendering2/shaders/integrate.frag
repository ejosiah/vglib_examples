#version 460
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "common.glsl"


layout(location = 0) in struct {
    vec3 direction;
} fs_in;

layout(location = 0) out vec4 fragColor;


void main() {
    rngState = initRNG(gl_FragCoord.xy, vec2(scene.width, scene.height), scene.currentFrame);

    vec3 wrO = scene.cameraPosition;
    vec3 wrd = normalize(fs_in.direction);

    vec3 rO = worldToVoxel(wrO);
    vec3 rd = directionWorldToVoxel(wrd);

    TimeSpan pTS;
    float phase_debug = 0;
    if(!test(rO, rd, vec3(0), vec3(1), pTS)) discard;

    vec3 entry_position = rO + rd * pTS.t0;
    float voxel_center = length(0.5/volumeDim());

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
        vec3 sample_position  = start + rd * (t + voxel_center * rand(rngState));

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