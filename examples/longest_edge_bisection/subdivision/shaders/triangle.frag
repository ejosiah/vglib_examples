#version 460

layout (location = 0) noperspective in vec3 i_Distance;
layout (location = 0) out vec4 o_FragColor;

void main() {
    vec4 color = vec4(190.0 / 255.0, 190.0 / 255.0, 190.0 / 255.0, 1.0);
    vec4 wire = vec4(0.0, 0.0, 0.0, 1.0);
    float wirescale = 2.0; // pixel width of the wireframe
    vec3 d2 = i_Distance * i_Distance;
    float nearest = min(min(d2.x, d2.y), d2.z);
    float f = exp2(-nearest / wirescale);

    o_FragColor = mix(color, wire, f);
}