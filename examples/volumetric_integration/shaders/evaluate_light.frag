#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#define MATERIAL_SET 1
#define MATERIAL_BINDING_POINT 0
#define LIGHT_BINDING_POINT 1
#define LIGHT_INSTANCE_BINDING_POINT 2
#define TEXTURE_INFO_BINDING_POINT 3


#define POSITION_INDEX 0
#define NORMAL_INDEX 1
#define BASE_COLOR_INDEX 2
#define MRO_INDEX 3
#define DEPTH_BUFFER_INDEX 4

#define ALPHA_MODE_OPAQUE 0
#define ALPHA_MODE_MASK 1
#define ALPHA_MODE_BLEND 2


#define POSITION_TEXTURE global_textures[POSITION_INDEX]
#define NORMAL_TEXTURE global_textures[NORMAL_INDEX]
#define BASE_COLOR_TEXTURE global_textures[BASE_COLOR_INDEX]
#define METAL_ROUGHNESS_AO_TEXTURE global_textures[MRO_INDEX]
#define DEPTH_BUFFER_TEXTURE global_textures[DEPTH_BUFFER_INDEX]


layout(set = 2, binding = 10) uniform sampler2D global_textures[];

#include "punctual_lights.glsl"

#include "evaluate_light.glsl"

layout(location = 0) in struct {
    vec2 uv;
    vec4 color;
} fs_in;



layout(push_constant) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
};

layout(location = 0) out vec4 fragColor;

void main() {
    const vec2 uv = fs_in.uv;

    vec3 position = texture(POSITION_TEXTURE, uv).xyz;
    vec3 normal = texture(NORMAL_TEXTURE, uv).xyz;
    vec4 baseColor = texture(BASE_COLOR_TEXTURE, uv);
    vec3 mro = texture(METAL_ROUGHNESS_AO_TEXTURE, uv).xyz;
    vec3 eyes = (inverse(view) * vec4(0, 0, 0, 1)).xyz;


    LightingParams lp = LightingParams(
        baseColor,
        position,
        normal,
        mro,
        eyes
    );

    vec3 color = evaluateLight(lp);

    color.rgb /= 1 + color.rgb;
    color.rgb = pow(color.rgb, vec3(0.4545));

    fragColor = vec4(color, baseColor.a);
    gl_FragDepth = texture(DEPTH_BUFFER_TEXTURE, uv).r;
}