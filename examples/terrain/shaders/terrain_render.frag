#version 460

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 color;
    vec2 uv;
} f;

layout(location = 0) out vec4 fragColor;

void main() {
//    fragColor = vec4(f.uv, 0, 1);
    fragColor = vec4(1);
}