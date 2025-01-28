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

void main() {
    vec3 rO = scene.cameraPosition;
    vec3 rd = normalize(fs_in.direction);

    TimeSpan pTS;
    if(!test(rO, rd, info.bmin, info.bmax, pTS)) discard;

    vec3 world_position = rO + rd * pTS.t0;

    float step_size = scene.primaryStepSize/float(scene.numSteps);
    const int num_steps = int(ceil(length(pTS)/step_size));
//    const int num_steps = int(1/step_size);
    const float p_stride = length(pTS)/num_steps;
//    const float p_stride = 1/num_steps;

    vec3 transmission = vec3(1);
    vec3 inScattering = vec3(0);
    vec3 sigma_a, sigma_s, sigma_t;

    vec3 start = worldToVoxel(world_position, rd);
    vec3 pos = worldToVoxel(world_position + rd, rd);
    rd = normalize(pos - start) * p_stride;

    for(int t = 0; t < num_steps; ++t) {
        vec3 sample_position  = start + rd * t;

        float density = getPerticipatingMedia(sample_position, sigma_s, sigma_a, sigma_t);
        if (density < cutoff ) continue;

        vec3 sample_transmission = exp(-sigma_t * p_stride);
        transmission *= sample_transmission;
        inScattering += transmission * sigma_s;

    }

    float alpha = 1 - dot(vec3(1), transmission/3);
    fragColor = vec4(inScattering, alpha);
    writeDepthValue(world_position);

}