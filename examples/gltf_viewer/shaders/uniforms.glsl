#ifndef UNIFORMS_GLSL
#define UNIFORMS_GLSL

#ifndef UNIFORMS_SET
#define UNIFORMS_SET 0
#define UNIFORMS_BINDING_POINT 0
#endif // UNIFORMS_SET

layout(set = UNIFORMS_SET, binding = UNIFORMS_BINDING_POINT) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
    int brdf_lut_texture_id;
    int irradiance_texture_id;
    int specular_texture_id;
    int framebuffer_texture_id;
    int g_buffer_texture_id;
    int discard_transmissive;
    int environment;
    int tone_map;
    int num_lights;
    int debug;
    int ibl_on;
    int direct_on;
    float ibl_intensity;
};

#define u_EnvIntensity ibl_intensity

#endif // UNIFORMS_GLSL