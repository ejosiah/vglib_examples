#version 460

#define SCENE_PAINT 2u

layout(set = 0, binding = 0) uniform sampler2D smokeField;

layout(push_constant) uniform Constants {
    mat4 transform;
    vec4 color;
    vec2 position;
    vec2 velocity;
    vec2 domainMin;
    vec2 domainMax;
    float radius;
    float size;
    uint scene;
};

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

vec3 getSciColor(float value, float minValue, float maxValue) {
    value = min(max(value, minValue), maxValue - 0.0001);
    float range = maxValue - minValue;
    value = range == 0.0 ? 0.5 : (value - minValue) / range;

    float interval = 0.25;
    int index = int(floor(value / interval));
    float t = (value - float(index) * interval) / interval;

    if(index == 0) return vec3(0.0, t, 1.0);
    if(index == 1) return vec3(0.0, 1.0, 1.0 - t);
    if(index == 2) return vec3(t, 1.0, 0.0);
    return vec3(1.0, 1.0 - t, 0.0);
}

void main() {
    vec3 smoke = texture(smokeField, uv).rrr;

    if(scene == SCENE_PAINT) {
        fragColor = vec4(getSciColor(smoke.r, 0.0, 1.0), smoke.r);
    } else {
        fragColor = vec4(smoke, 1.0);
    }
}
