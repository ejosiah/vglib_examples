#version 460
#extension GL_EXT_nonuniform_qualifier : enable

#include "octahedral.glsl"

layout(set = 0, binding = 10) uniform sampler2D global_textures[];

layout(set = 1, binding = 0) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
    int brdf_lut_texture_id;
    int irradiance_texture_id;
    int specular_texture_id;
    int framebuffer_texture_id;
    int discard_transmissive;
    int environment;
    int tone_map;
};

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 sampleSphere(vec3 v){
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

layout(location = 0) in vec3 texCord;

layout(location = 0) out vec4 fragColor;

void main() {
    vec2 uv = 0.5 + 0.5 * octEncode(normalize(texCord));
    vec3 color = texture(global_textures[environment], uv).rgb;

    if(tone_map == 1){
        color /= color + 1;
        color = pow(color, vec3(0.454545));
    }

    fragColor = vec4(color, 1);
}