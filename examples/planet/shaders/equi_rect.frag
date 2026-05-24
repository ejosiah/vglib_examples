#version 460 core

layout(set = 0, binding = 0) uniform sampler2D equirectangular_map;

layout(location = 0) in vec3 texCoord;

layout(location = 0) out vec4 fragColor;

vec2 dirToEquirectUV(vec3 dir) {
    dir = normalize(dir);

    float u = atan(dir.z, dir.x) * (1.0 / (2.0 * 3.14159265359)) + 0.5;
    float v = asin(clamp(dir.y, -1.0, 1.0)) * (1.0 / 3.14159265359) + 0.5;

    return vec2(u, v);
}

void main() {
    vec2 uv = dirToEquirectUV(texCoord);
    vec3 color = texture(equirectangular_map, uv).rgb;
    fragColor = vec4(color, 1);
}