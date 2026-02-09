#version 460

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout: enable

#include "uniforms.glsl"

layout(set = 1, binding = 10) uniform sampler2D global_textures[];

layout (location = 0) in vec2 uv;
layout (location = 1) flat in uint texture_id;

layout(location = 0) out vec4 fragColor;

float linearizeDepth(float z){
//    return (ubo.near * ubo.far) / (z * (ubo.far - ubo.near) - ubo.far);

    return (2.0 * ubo.near) / (ubo.far + ubo.near - z * (ubo.far - ubo.near));
}

void main() {
    vec2 st = uv;
    st.y = 1 - st.y;
    float raw_depth = texture(global_textures[texture_id], st).r;

    float depth = linearizeDepth(raw_depth);
    fragColor = vec4(1 - vec3(depth), 1);

}