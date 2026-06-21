#version 460 core
#define PI 3.14159265358979

layout(set = 0, binding = 0) uniform sampler3D smokeField;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform Constants {
    vec3 dye;
};

const float ppm = 8;

void main(){
    float density = texture(smokeField, vec3(vUv, 0.5)).y;
    density *= ppm;
    density /= (1 + density);

    vec3 smoke = dye * density;
    fragColor = vec4(smoke, density);
    fragColor /= (1 + fragColor);
}
