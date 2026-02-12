#ifndef CAMERA_UNIFORM_GLSL
#define CAMERA_UNIFORM_GLSL

#extension GL_EXT_scalar_block_layout: enable

#ifndef CAMERA_SET
#define CAMERA_SET 0
#endif

layout(set = CAMERA_SET, binding = 0, scalar) uniform Camera_Uniforms {
    mat4 projection;
    mat4 view;
    mat4 model;
    mat4 inverseView;
    mat4 inverseProjection;
    mat4 inverseViewProjection;
    mat4 previousViewProjection;
    vec2 viewportSize;
    float near;
    float far;
} camera;

vec2 uv_nearest( ivec2 pixel, vec2 texture_size ) {
    vec2 uv = floor(pixel) + .5;

    return uv / texture_size;
}

vec3 ndc_from_uv_raw_depth( vec2 uv, float raw_depth ) {
    return vec3( uv.x * 2 - 1, (1 - uv.y) * 2 - 1, raw_depth );
}

vec3 world_position_from_depth( vec2 uv, float raw_depth ) {

    vec4 H = vec4( ndc_from_uv_raw_depth(uv, raw_depth), 1.0 );
    vec4 D = camera.inverseViewProjection * H;

    return D.xyz / D.w;
}

vec3 view_position_from_depth( vec2 uv, float raw_depth ) {

    vec4 H = vec4( ndc_from_uv_raw_depth(uv, raw_depth), 1.0 );
    vec4 D = camera.inverseProjection * H;

    return D.xyz / D.w;
}

#endif // CAMERA_UNIFORM_GLSL