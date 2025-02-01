#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#define MATERIAL_SET 1
#define MATERIAL_BINDING_POINT 0

#define MATERIAL_ID meshes[nonuniformEXT(drawId)].materialId
#define MATERIAL materials[MATERIAL_ID]

#define SHADOW_MAP global_textures_array[0]

#include "gltf.glsl"
#include "cascade_shadow_map.glsl"

layout(set = 0, binding = 0) buffer MeshData {
    Mesh meshes[];
};

layout(set = MATERIAL_SET, binding = MATERIAL_BINDING_POINT) buffer GLTF_MATERIAL {
    Material materials[];
};

layout(set = 2, binding = 10) uniform sampler2DArray global_textures_array[];

layout(set = 3, binding = 0, scalar) buffer SHAODW_MAP_DATA {
    vec3 lightDir;
    int numCascades;
    int usePCF;
    int colorCascades;
    int showExtents;
    int shadowOn;
} ubo;

layout(set = 3, binding = 1, std430) buffer Cascades {
    mat4 cascadeViewProjMat[];
};

layout(set = 3, binding = 2, scalar) buffer CascadesSplits {
    float cascadeSplits[];
};

layout(location = 0) in struct {
    vec4 color;
    vec3 localPos;
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv[2];
} fs_in;

layout(location = 13) in flat int drawId;

layout(location = 0) out vec4 fragColor;

const float Ambient = 0;


vec3 cascadeColors[6] = vec3[6](
    vec3(1.0f, 0.25f, 0.25f),
    vec3(0.25f, 1.0f, 0.25f),
    vec3(0.25f, 0.25f, 1.0f),
    vec3(1.0f, 1.0f, 0.25f),
    vec3(0.25f, 1.0f, 1.0f),
    vec3(1.0f, 0.25f, 1.0f)
);

vec3 frustumCorners[8] = vec3[8](
    vec3(-1.0f,  1.0f, 0.0f),
    vec3( 1.0f,  1.0f, 0.0f),
    vec3( 1.0f, -1.0f, 0.0f),
    vec3(-1.0f, -1.0f, 0.0f),
    vec3(-1.0f,  1.0f,  1.0f),
    vec3( 1.0f,  1.0f,  1.0f),
    vec3( 1.0f, -1.0f,  1.0f),
    vec3(-1.0f, -1.0f,  1.0f)
);

float computeVisibility(out uint cascadeIndex) {
    cascadeIndex = 0;
    for(int i = 0; i < ubo.numCascades - 1; ++i) {
        if(fs_in.position.z < cascadeSplits[i]) {
            cascadeIndex = i + 1;
        }
    }

    vec4 lightSpacePos = (cascadeViewProjMat[cascadeIndex] * vec4(fs_in.position, 1));
    float shadow = ubo.usePCF == 1 ?
        pcfFilteredShadow(SHADOW_MAP, lightSpacePos, cascadeIndex)
        : shadowCalculation(SHADOW_MAP, lightSpacePos, cascadeIndex);

    return 1 - shadow;
}

void main() {
    vec3 L = ubo.lightDir;
    vec3 N = normalize(fs_in.normal);
    vec3 albedo = MATERIAL.baseColor.rgb;

    float diffuse = max(0, dot(N, L));


    vec3 color = (Ambient + diffuse) * albedo;

    uint cascadeIndex;
    color *= ubo.shadowOn == 1 ? computeVisibility(cascadeIndex) : 1;

    if(ubo.shadowOn == 1 && ubo.colorCascades == 1) {
        if(ubo.showExtents == 1) {
            vec3 bmin = vec3(1e10);
            vec3 bmax = vec3(1e-10);

            mat4 lightToWorld = inverse(cascadeViewProjMat[cascadeIndex]);
            for(int i = 0; i < 8; ++i) {
                vec4 p = lightToWorld * vec4(frustumCorners[i], 1);
                p /= p.w;
                bmin = min(bmin, p.xyz);
                bmax = max(bmax, p.yxz);
            }
            if(all(greaterThan(fs_in.position, bmin)) && all(lessThan(fs_in.position, bmax))){
                color = cascadeColors[cascadeIndex];
            }
        }else {
            color = cascadeColors[cascadeIndex];

        }
    }

    fragColor = vec4(color, 1);
}