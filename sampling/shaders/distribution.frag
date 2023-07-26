#version 460

layout(set = 0, binding = 0) uniform sampler2D envMap;
layout(set = 0, binding = 1) uniform sampler2D envMapDistribution;
layout(set = 0, binding = 2) uniform sampler1D envMarginalProbablity;

layout(push_constant) uniform Constants {
    int width;
    int height;
};

layout(location = 0) out vec4 fragColor;

vec2 remap(vec2 a, vec2 b, vec2 c, vec2 d, vec2 x){
    vec2 t = (x - a)/(b - a);
    t = clamp(t, vec2(-0.5), vec2(1.5));
    return mix(c, d, t);
}

float remap(float a, float b, float c, float d, float x){
    float t = (x - a)/(b - a);
    t = clamp(t, -0.5, 1.5);
    return mix(c, d, t);
}

void main(){
    vec2 uv = gl_FragCoord.xy/vec2(width, height);
    fragColor = vec4(1);
    if(uv.x >= 0.1 && uv.x <= 0.9 && uv.y >= 0.1 && uv.y <= 0.9){
        vec2 _uv = remap(vec2(0.1), vec2(0.9), vec2(0), vec2(1), uv);
        fragColor = texture(envMapDistribution, _uv).rrrr;
    }else if(uv.x >= 0.01 && uv.x <= 0.05 && uv.y >= 0.1 && uv.y <= 0.9) {
        vec2 _uv = remap(vec2(0.1), vec2(0.9), vec2(0), vec2(1), uv);
        fragColor = texture(envMarginalProbablity, _uv.y).rrrr;
    }
}