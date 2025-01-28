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
    gl_FragDepth = 1;

    vec3 o = scene.cameraPosition;
    vec3 rd = normalize(fs_in.direction);

    const float isoValue = scene.isoLevel;

    TimeSpan span;
    if(test(o, rd, info.bmin, info.bmax, span)) {
        vec3 pos = o * rd * span.t0;
        ivec3 voxelDim = textureSize(DENSITY_TEXTURE, 0);
        int maxSteps = max(voxelDim.x, max(voxelDim.y, voxelDim.z));
        float delta = 1/float(maxSteps);

        vec3 start = (info.worldToVoxelTransform * vec4(pos, 1)).xyz + sign(rd) * 0.5/vec3(voxelDim);
        pos = (info.worldToVoxelTransform * vec4(pos + rd, 1)).xyz + sign(rd) * 0.5/vec3(voxelDim);

        rd = normalize(pos - start) * delta;
        for(int i = 0; i < maxSteps; ++i) {
            vec3 pos0 = start + rd * i;

            if(outOfBounds(pos0)) break;

            vec3 pos1 = pos + rd;
            float val = sampleDensity(pos0);
            float val1 = sampleDensity(pos0 + rd);

            if((val - isoValue) > 0 && (val1 - isoValue) <= 0) {
                pos = (info.voxelToWorldTransform * vec4(pos0, 1)).xyz;
                vec4 clipPos = proj * view * model * vec4(pos, 1);
                clipPos /= clipPos.w;

                vec3 N = -computeNormal(pos0, pos1, isoValue);
                vec3 L = normalize(vec3(1));

                fragColor.rgb = vec3(1, 1, 0) * max(0, dot(N, L));
                gl_FragDepth = clipPos.z;
                break;
            }
        }
    }
}