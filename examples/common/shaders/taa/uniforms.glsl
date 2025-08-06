#ifndef TTA_UNIFORMS_GLSL
#define TTA_UNIFORMS_GLSL

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

layout(set = 0, binding = 0, scalar) uniform Constants {
    mat4 current_view_projection;
    mat4 inverse_current_view_projection;

    mat4 previous_view_projection;
    mat4 inverse_previous_view_projection;

    vec2 jitter_xy;
    vec2 previous_jitter_xy;

    vec2 resolution;
    uint color_buffer_index;
    uint depth_buffer_index;
    uint velocity_texture_index;
    uint history_color_texture_index;

    uint resolve_image_index;
    uint velocity_image_index;
} uniforms;

layout(set = 1, binding = 10) uniform sampler2D global_textures[];
layout(set = 1, binding = 11, rg16f) uniform image2D global_images_rg16f[];
layout(set = 1, binding = 11, rgba32f) uniform image2D global_images[];

#define color_buffer global_textures[nonuniformEXT(uniforms.color_buffer_index)]
#define depth_buffer global_textures[nonuniformEXT(uniforms.depth_buffer_index)]
#define velocity_buffer global_textures[nonuniformEXT(uniforms.velocity_texture_index)]
#define velocity_out global_images_rg16f[nonuniformEXT(uniforms.velocity_image_index)]

#define history_buffer global_textures[nonuniformEXT(uniforms.history_color_texture_index)]
#define resolve_image global_images[nonuniformEXT(uniforms.resolve_image_index)]

#endif// TTA_UNIFORMS_GLSL
