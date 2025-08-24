#version 460

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 normal;
    vec3 color;
    vec2 uv;
} fs_in;

layout(location = 4) noperspective in vec3 distance;

layout(location = 0) out vec4 fragColor;


void main() {
    fragColor = vec4(1);

    const float wireScale = 1.1; // scale of the wire in pixel
    vec4 wireColor = vec4(0.0, 0.0, 0.0, 1.0);
    vec3 distanceSquared = distance * distance;
    float nearestDistance = min(min(distanceSquared.x, distanceSquared.y), distanceSquared.z);
    float blendFactor = exp2(-nearestDistance / wireScale);

    vec3 L = normalize(vec3(1));
    vec3 N = normalize(fs_in.normal);
    vec3 albedo = fs_in.color;

    vec3 radiance = albedo * max(0, dot(N, L));
    radiance = pow(radiance, vec3(0.454));
    fragColor.rgb = mix(radiance, wireColor.xyz, blendFactor);
}