#ifndef SCENE_GLSL
#define SCENE_GLSL

layout(set = 0, binding = 0) uniform SceneConstants {
    mat4 current_view_projection;
    mat4 inverse_current_view_projection;

    mat4 previous_view_projection;
    mat4 inverse_previous_view_projection;

    mat4 world_to_camera;
    vec4 camera_position;
    vec3 camera_direction;
    int current_frame;

    vec2 jitter_xy;
    vec2 previous_jitter_xy;

    vec2 resolution;
    int color_buffer_index;
    int depth_buffer_index;
} scene;

layout(set = 0, binding = 1) uniform TaaConstants {
    uint history_color_texture_index;
    uint output_texture_index;
    uint velocity_texture_index;
    uint current_color_texture_index;
} taa;

#endif // SCENE_GLSL