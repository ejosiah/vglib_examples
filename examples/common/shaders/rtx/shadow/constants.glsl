#ifndef SHADOW_CONSTANTS_GLSL
#define SHADOW_CONSTANTS_GLSL

#ifndef SHADOW_CONSTANTS_SET
#define SHADOW_CONSTANTS_SET 0
#endif // SHADOW_CONSTANTS_SET

layout(set = SHADOW_CONSTANTS_SET, binding = 0) uniform ShadowConstants {
    float resolution_scale;
    float resolution_scale_rcp;
    uint depth_texture_index;
    uint normal_buffer_index;
    uint normals_texture_index;

    uint motion_vectors_texture_index;
    uint visibility_cache_texture_index;
    uint variation_texture_index;
    uint variation_cache_texture_index;
    uint filtered_variation_texture_index;
    uint filtered_visibility_texture_index;
    uint samples_count_cache_texture_index;

    uint motion_vector_image_index;
    uint view_normal_image_index;
    uint filtered_variation_image_index;
    uint filtered_visibility_image_index;
    uint samples_count_cache_image_index;
    uint visibility_cache_image_index;
    uint variation_image_index;
    uint variation_cache_image_index;
    uint frame_index;

};

#endif // SHADOW_CONSTANTS_GLSL