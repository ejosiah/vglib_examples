#version 460 core
#define PI 3.14159265358979

layout(set = 0, binding = 0) uniform sampler2D smokeField;
layout(set = 1, binding = 0) uniform sampler2D smokeField1;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform Constants {
    vec3 dye;
};

float remap(float x, float a, float b, float c, float d) {
    return mix(c, d, (x - a)/(b - a));
}

const float ppm = 1E6;

void main(){
    vec4 debug;
    vec2 uv = vUv;

    if(uv.x < 0.5) {
        uv.x = remap(uv.x, 0, 0.5, 0, 1);
        debug = texture(smokeField, uv);
    }else {
        uv.x = remap(uv.x, 0.5, 1, 0, 1);
        debug = texture(smokeField1, uv);
    }

    fragColor /= (1 + fragColor);
    fragColor = debug * 1000;
    fragColor /= (1 + fragColor);
}