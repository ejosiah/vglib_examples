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

    TimeSpan ts;
    float phase_debug = 0;
    if(!test(rO, rd, vec3(0), vec3(1), ts)) discard;

    vec3 entry_position = rO + rd * ts.t0;

    float step_size = scene.primaryStepSize/float(scene.numSteps);
    const int num_steps = int(ceil(length(ts)/step_size));

    vec3 transmission = vec3(1);
    vec3 inScattering = vec3(0);
    vec3 sigma_a, sigma_s, sigma_t;
    const float cos0 = dot(-wrd, light_dir);
    
    float in_scatter_probablity = phaseHG(g, cos0);
    phase_debug += in_scatter_probablity;

    float tMin = ts.t0;
    for(int i = 0; i < num_steps; ++i) {

        float t;
        float density = sampleDensityDT(rO, rd, ts, t);
        sigma_a = scene.absorption * density;
        sigma_s = scene.scattering * density;
        sigma_t = max(EPSILION_VEC3, sigma_s + sigma_a);

        tMin = min(tMin, t);

        vec3 sample_position = rO + rd * t;

        const float stride = ts.t1 - t;
        vec3 sample_transmission = exp(-sigma_t * stride);
        vec3 light_attenuation = compute_light_attenuation(sample_position, step_size);

        vec3 light_intensity = light_color * light_attenuation;
        inScattering += sample_transmission * sigma_s * in_scatter_probablity * light_intensity * stride;
        transmission *= sample_transmission;

    }

    vec3 world_position = voxelToWorld(rO + rd * tMin);

    float alpha = 1 - dot(vec3(1), transmission/3);
    fragColor = vec4(inScattering, alpha);
    writeDepthValue(world_position);

}