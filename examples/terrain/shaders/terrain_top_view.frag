#version 460

layout (location = 0) flat in int i_IsVisible;
layout (location = 1) noperspective in vec3 i_Distance;
layout (location = 0) out vec4 fragColor;

void main() {
#if 0
    vec4 wire = i_IsVisible > 0 ? vec4(0.00,0.20,0.70, 0.5) : vec4(0.40,0.40,0.40,0.5);
    vec4 color = i_IsVisible > 0 ? vec4(0.75,0.80,0.93, 0.5) : vec4(0.85,0.85,0.85, 0.5);
#else
    vec4 wire = vec4(0.0, 0.0, 0.0, 0.5);
    vec4 color = vec4(0.85,0.85,0.85, 0.5);
#endif
    const float wirescale = 1.5; // scale of the wire
    vec3 d2 = i_Distance * i_Distance;
    float nearest = min(min(d2.x, d2.y), d2.z);
    float f = exp2(-nearest / wirescale);

    fragColor = mix(color, wire, f);
}