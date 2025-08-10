#ifndef SSS_UNIFORMS_GLSL
#define SSS_UNIFORMS_GLSL

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "octahedral.glsl"
#include "punctual_lights.glsl"

layout(set = 0, binding = 0, scalar) uniform Uniforms {
    mat4 lightSpaceMatrix;
    mat3 envRotation;
    vec2 pixelSize;
    float sssWidth;
    float specularRoughness;
    float specularIntensity;
    float bumpiness;
    float ambientFactor;
    float translucency;
    float near;
    float far;
    uint diffuse_tex_id;
    uint specular_tex_id;
    uint color_tex_id;
    uint depth_tex_id;
    uint sss_tex_id;
    uint sss_image_id;
    uint sss_enabled;
} uniforms;

layout(set = 0, binding = 1, scalar) uniform LightUnform {
    Light light;
};

#define u_EnvRotation uniforms.envRotation

float clampedDot(vec3 a, vec3 b) {
    return clamp(dot(a, b), 0, 1);
}

float linearizeDepth(float z){
    const float near = uniforms.near;
    const float far = uniforms.far;

    return (near * far) / (far + near - z * (far - near));
}

bool sssEnaled() { return uniforms.sss_enabled == 1; }

#endif // SSS_UNIFORMS_GLSL