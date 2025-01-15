#version 460


layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 fragColor;


void main() {
    float r = length(2 * gl_PointCoord.xy - 1);

    if(r > 1) discard;

    vec4 innerColor = vec4(0, 1, 1, 1);
    vec4 outerColor = vec4(0, 0, 0, 1);
    vec4 color = mix(innerColor, outerColor, smoothstep(0.5, 0.6, r));

    fragColor = color;
}