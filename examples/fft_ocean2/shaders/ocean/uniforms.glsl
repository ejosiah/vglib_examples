#ifndef OCEAN_UNIFORMS_GLSL
#define OCEAN_UNIFORMS_GLSL

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#ifndef OCEAN_UNIFORM_SET
#   error User must specify the set location for ocean uniforms
#endif

#define WIRE_FRAME_ON 1u
#define TILES_ON 2u
#define DEBUG_NORMALS 4u

layout(set = OCEAN_UNIFORM_SET, binding = 0, scalar) buffer Uniforms {
    mat4 modelMatrix;
    mat4 modelViewMatrix;
    mat4 viewMatrix;
    mat4 cameraMatrix;
    mat4 viewProjectionMatrix;
    mat4 modelViewProjectionMatrix;
    vec4 frustumPlanes[6];
    vec4 horizontalLength;
    vec4 camera;
    vec4 lightDirection;
    vec4 earthCenter;
    vec4 scatterColor;
    vec4 scatterConstants;
    ivec4 mouse;
    vec2 heightMinMax[4];
    vec2 screenResolution;
    vec2 sunSize;
    float lodFactor;
    float minLodVariance;
    float dmapFactor;
    float choppiness;
    float rho;
    float sigma;
    float near;
    float far;
    float normalFallOff;
    uint tileCount;
    uint flags;
    uint heightMapIndex;
    uint normalMapIndex;
} u;


#ifndef BINDLESS_DESCRIPTOR_SET
#   error User must specify the set location for bindless descriptor
#endif
layout(set = BINDLESS_DESCRIPTOR_SET, binding = 10) uniform sampler2D global_textures[];
layout(set = BINDLESS_DESCRIPTOR_SET, binding = 10) uniform sampler2DArray global_textures_array[];

#define u_DmapSampler global_textures_array[nonuniformEXT(u.heightMapIndex)]
#define u_NormalSampler global_textures_array[nonuniformEXT(u.normalMapIndex)]

bool wireframeEnabled() {
    return (u.flags & WIRE_FRAME_ON) == WIRE_FRAME_ON;
}

bool showTiles() {
    return (u.flags & TILES_ON) == TILES_ON;
}

bool showNormals() {
    return (u.flags & DEBUG_NORMALS) == DEBUG_NORMALS;
}
vec3 sampleDisplacement(vec2 p) {
    float H = 0;
    float Dx = 0;
    float Dz = 0;

    const uint tileCount = u.tileCount;
    for(uint i = 0; i < tileCount; ++i){
        vec2 uv = fract(p/u.horizontalLength[i]);
        vec3 loc = vec3(uv, i);
        vec3 disp = texture(u_DmapSampler, loc).xyz;

        H += disp.y;
        Dx += disp.x * u.choppiness;
        Dz += disp.z * u.choppiness;
    }

    return vec3(Dx, H, Dz) * u.dmapFactor;
}

#endif // OCEAN_UNIFORMS_GLSL