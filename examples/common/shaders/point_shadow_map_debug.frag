#version 460 core

layout(set = 0, binding = 1) uniform samplerCube skybox;

layout(location = 0) in vec3 texCoord;

layout(location = 0) out vec4 fragColor;

void main(){
    float dist = texture(skybox, texCoord).r;
    dist /= dist + 1;
    fragColor = vec4(dist);
}