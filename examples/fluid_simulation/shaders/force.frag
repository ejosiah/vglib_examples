#version 450 core

layout(set = 0, binding = 0) uniform sampler2D forceField;

layout(push_constant) uniform Constants{
    vec2 force;
    vec2 center;
    float radius;
    float dt;
};

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 du;

vec2 accumForce(vec2 coord){
    return texture(forceField, fract(coord)).xy;
}

vec2 periodicDelta(vec2 from, vec2 to) {
    vec2 delta = from - to;
    return delta - round(delta);
}

void main(){
    vec2 d = periodicDelta(center, uv);
    du.xy = force * exp(-dot(d, d)/radius);
    du.xy /= dt;
    du.xy += accumForce(uv);
}
