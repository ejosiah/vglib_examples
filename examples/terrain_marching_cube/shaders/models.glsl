#ifndef MODELS_GLSL
#define MODELS_GLSL

#ifndef CAMERA_SET_BINDING_INFO
#define CAMERA_SET_BINDING_INFO

#define CAMERA_SET 0
#define CAMERA_INFO_BINDING 0

#endif // CAMERA_SET_BINDING_INFO

#ifndef VERTEX_DATA_BINDING_INFO
#define VERTEX_DATA_BINDING_INFO

#define VERTEX_DATA_SET 1
#define VERTEX_DATA_BINDING 0

#endif // VERTEX_DATA_BINDING_INFO

layout(set = CAMERA_SET, binding = CAMERA_INFO_BINDING, scalar) uniform uboCameraInfo {
    mat4 view_projection;
    mat4 inverse_view_projection;
    mat4 grid_to_world;
    vec4 frustum[6];
    vec3 position;
    vec3 aabbMin;
    vec3 aabbMax;
} camera_info;

layout(set = VERTEX_DATA_SET, binding = VERTEX_DATA_BINDING, scalar) buffer vertexDataBuffer {
    vec3 position;
    vec2 normal;
    float ambient_occulsion;
} vertex[];

#endif // MODELS_GLSL