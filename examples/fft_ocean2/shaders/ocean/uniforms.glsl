#ifndef OCEAN_UNIFORMS_GLSL
#define OCEAN_UNIFORMS_GLSL

#extension GL_EXT_scalar_block_layout : enable

#ifndef OCEAN_UNIFORM_SET
#   error User must specify the set location for ocean uniforms
#endif

layout(set = OCEAN_UNIFORM_SET, binding = 0, scalar) buffer Uniforms {
    mat4 modelMatrix;
    mat4 modelViewMatrix;
    mat4 viewMatrix;
    mat4 cameraMatrix;
    mat4 viewProjectionMatrix;
    mat4 modelViewProjectionMatrix;
    vec4 frustumPlanes[6];
    vec4 horizontalLength;
    vec2 dimensions;
    float lodFactor;
    float minLodVariance;
    float dmapFactor;
    float choppiness;
    uint tileCount;
    uint heightMapIndex;
    uint normalMapIndex;
} u;


#ifndef BINDLESS_DESCRIPTOR_SET
#   error User must specify the set location for bindless descriptor
#endif
layout(set = BINDLESS_DESCRIPTOR_SET, binding = 10) uniform sampler2D global_textures[];
layout(set = BINDLESS_DESCRIPTOR_SET, binding = 10) uniform sampler2DArray global_textures_array[];

#endif // OCEAN_UNIFORMS_GLSL