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

const float ppm = 5;

void main(){
    float density = 0;
    vec2 uv = vUv;
    if(uv.x < 0.5) {
        uv.x = remap(uv.x, 0, 0.5, 0, 1);
        density = texture(smokeField, uv).y;
    }else {
        uv.x = remap(uv.x, 0.5, 1, 0, 1);
        density = texture(smokeField1, uv).y;
    }
    density *= ppm;
    density /= (1 + density);

    vec3 smoke = dye * density;
    fragColor = vec4(smoke, density);
    fragColor /= (1 + fragColor);

    vec2 d = vec2(0.5, 0.98) - uv;
}