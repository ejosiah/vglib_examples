#version 460
#extension GL_EXT_scalar_block_layout : enable

layout(set = 0, binding = 0) uniform sampler3D sourceField;

layout(push_constant, scalar) uniform Constants{
    vec3 sourceColor;
    vec2  source;
    float radius;
    float dt;
};

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;

vec3 accumColor(vec2 coord){
    return texture(sourceField, vec3(fract(coord), 0.5)).rgb;
}

vec2 periodicDelta(vec2 from, vec2 to) {
    vec2 delta = from - to;
    return delta - round(delta);
}

void main(){
    vec2 sourceUv = gl_FragCoord.xy / vec2(textureSize(sourceField, 0).xy);
    vec2 d = periodicDelta(source, sourceUv);
    vec3 dye = sourceColor.rgb * exp(-dot(d, d)/radius);
    dye /= dt;
    dye += accumColor(sourceUv);
    color = vec4(dye, 0);
}
