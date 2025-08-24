#version 460

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 normal;
    vec3 color;
    vec2 uv;
} f;

layout(location = 0) out vec4 fragColor;


void main() {
//    fragColor = vec4(f.uv, 0, 1);
    vec3 L = normalize(vec3(1));
    vec3 N = normalize(f.normal);
    vec3 albedo = f.color;

    vec3 radiance = albedo * max(0, dot(N, L));
    radiance = pow(radiance, vec3(0.454));
    fragColor = vec4(radiance, 1);
}