#version 460

layout (location = 0) noperspective in vec3 i_Distance;
layout (location = 0) out vec4 fragColor;

void main() {
    vec4 wire = vec4(0.0, 0.0, 0.0, 0.5);
    vec4 color = vec4(0.85,0.85,0.85, 0.5);
    const float wirescale = 1.5; // scale of the wire
    vec3 d2 = i_Distance * i_Distance;
    float nearest = min(min(d2.x, d2.y), d2.z);
    float f = exp2(-nearest / wirescale);

    fragColor = mix(color, wire, f);
}