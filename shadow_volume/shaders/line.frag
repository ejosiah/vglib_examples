#version 460 core

layout(set = 0, binding = 0) uniform UBO {
    vec3 lightDirection;
    vec3 cameraPosition;
};

layout(location = 0) out vec4 fracColor;

void main(){
    fracColor = vec4(1, 0, 0, 0.2);
}