#version 460
#extension GL_EXT_scalar_block_layout : enable

layout(set = 0, binding = 0) uniform sampler2D sourceField;

layout(push_constant, scalar) uniform Constants{
    vec3 sourceColor;
    vec2  source;
    float radius;
    float dt;
};

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

vec3 accumColor(vec2 coord){
    return texture(sourceField, fract(coord)).rgb;
}

void main(){
    vec2 d = (source - uv);
    color.rgb = sourceColor.rgb * exp(-dot(d, d)/radius);
    color.rgb /= dt;
    color.rgb += accumColor(uv);
}