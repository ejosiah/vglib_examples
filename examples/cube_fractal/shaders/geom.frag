#version 460

layout(location = 0) in struct {
    vec3 normal;
    vec3 fragPos;
    vec3 camPos;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main() {
    vec3 N = normalize(fs_in.normal);
    vec3 L = normalize(fs_in.camPos - fs_in.fragPos);
    vec3 radiance = max(0, dot(N, L)) * vec3(1);
    fragColor = vec4(radiance, 1);
}