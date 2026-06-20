#version 460

layout(location = 0) in vec4 color;
layout(location = 1) flat in float radius;
layout(location = 0) out vec4 fragColor;

void main() {

    float padding = 0.02;
    float d = length(gl_PointCoord * 2.0 - 1.0);
    if (d > 1.0) {
        discard;
    }
    d -= 0.98;
    float outline =  smoothstep(0.01, 0.02, abs(d));
    vec3 col = color.rgb * outline;
    fragColor = vec4(col, 1);

}
