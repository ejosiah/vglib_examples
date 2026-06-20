#version 460 core

layout(set = 0, binding = 0) uniform sampler2D temperatureField;

layout(push_constant) uniform Constants{
    float minTemp;
    float maxTemp;
};

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

vec3 tenMinutePhysicsColor(float t) {
    t = clamp(t, 0.0, 0.999999);

    float band = floor(4.0 * t);
    float localT = fract(4.0 * t);

    if(band < 1.0) {
        return vec3(0.0, localT, 1.0);
    }
    if(band < 2.0) {
        return vec3(0.0, 1.0, 1.0 - localT);
    }
    if(band < 3.0) {
        return vec3(localT, 1.0, 0.0);
    }
    return vec3(1.0, 1.0 - localT, 0.0);
}

void main(){
    float temp = texture(temperatureField, vUv).x;
    float level = (temp - minTemp) / (maxTemp - minTemp);
    fragColor = vec4(tenMinutePhysicsColor(level), 1.0);
}
