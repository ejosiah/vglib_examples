#version 460

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 lightPos;
} fs_in;

layout(location = 0) out float pointToLightDistance;

void main() {
    pointToLightDistance = distance(fs_in.worldPos, fs_in.lightPos);
}