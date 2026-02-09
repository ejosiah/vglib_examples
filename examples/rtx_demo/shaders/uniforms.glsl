#ifndef UNIFORMS_GLSL
#define UNIFORMS_GLSL

#ifndef UNIFORM_SET
#define UNIFORM_SET 0
#endif


layout(set = UNIFORM_SET, binding = 0, scalar) uniform Uniforms {
    mat4 projection;
    mat4 view;
    mat4 model;
    mat4 inverseProjection;
    mat4 inverseView;
    mat4 previousViewProjection;
    vec2 viewportSize;
    float near;
    float far;
} ubo;

#endif // UNIFORMS_GLSL