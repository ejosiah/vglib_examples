#version 460

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(set = 0, binding = 0) buffer InstanceData {
    mat4 models[];
} instanceData;

layout(push_constant) uniform Camera {
    mat4 model;
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) out struct {
    vec3 worldPos;
    vec3 normal;
    vec3 viewPos;
    float ring;
} vs_out;

void main() {
    mat4 instanceModel = instanceData.models[gl_InstanceIndex];
    vec4 worldPos = instanceModel * position;
    mat3 normalMatrix = transpose(inverse(mat3(instanceModel)));

    vs_out.worldPos = worldPos.xyz;
    vs_out.normal = normalize(normalMatrix * normal);
    vs_out.viewPos = (inverse(camera.view) * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    vs_out.ring = fract(float(gl_InstanceIndex) * 0.61803398875);

    gl_Position = camera.proj * camera.view * worldPos;
}
