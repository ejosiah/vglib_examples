#ifndef UNIFORMS_GLSL
#define UNIFORMS_GLSL

#ifndef UNIFORM_SET
#define UNIFORM_SET 0
#endif

layout(set = UNIFORM_SET, binding = 0) uniform Uniforms {
    uint indirect_light_texture_index;
};

#endif // UNIFORMS_GLSL