#version 460
#extension GL_EXT_scalar_block_layout : enable

#include "common.glsl"

layout(push_constant) uniform  Constants {
    mat4 model;
    mat4 view;
    mat4 proj;
};


layout(location = 0) in struct {
    vec3 direction;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_FragDepth = 1;

    vec3 o = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    vec3 rd = normalize(fs_in.direction);

    vec3 pos;
    const float isoValue = 0;

    if(test(o, rd, pos, info.bmin, info.bmax)) {
        ivec3 voxelDim = textureSize(density, 0);
        int maxSteps = max(voxelDim.x, max(voxelDim.y, voxelDim.z));
        float delta = 1/float(maxSteps);

        vec3 start = (info.worldToVoxelTransform * vec4(pos, 1)).xyz + sign(rd) * 0.5/vec3(voxelDim);
        vec3 pos = (info.worldToVoxelTransform * vec4(pos + rd, 1)).xyz + sign(rd) * 0.5/vec3(voxelDim);

        rd = normalize(pos - start) * delta;
        for(int i = 0; i < maxSteps; ++i) {
            vec3 pos0 = start + rd * i;

            if(outOfBounds(pos0)) break;

            vec3 pos1 = pos + rd;
            float val = texture(density, pos0).r;
            float val1 = texture(density, pos0 + rd).r;

            if((val - isoValue) < 0 && (val1 - isoValue) >= 0) {
                pos = (info.voxelToWordTransform * vec4(pos0, 1)).xyz;
                vec4 clipPos = proj * view * model * vec4(pos, 1);
                clipPos /= clipPos.w;

                vec3 N = -computeNormal(pos0, pos1, isoValue);
                vec3 L = normalize(vec3(1));

                fragColor.rgb = vec3(0, 1, 1) * max(0, dot(N, L));
                gl_FragDepth = clipPos.z;
                break;
            }
        }
    }
}