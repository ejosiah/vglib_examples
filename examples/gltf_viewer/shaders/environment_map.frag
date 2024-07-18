#version 460
#extension GL_EXT_nonuniform_qualifier : enable

#include "octahedral.glsl"

#define UNIFORMS_SET 1
#define UNIFORMS_BINDING_POINT 0
#include "uniforms.glsl"

layout(set = 0, binding = 10) uniform sampler2D global_textures[];


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
    vec3 color = texture(global_textures[environment], uv).rgb * ibl_intensity;


    fragColor = vec4(color, 1);
}