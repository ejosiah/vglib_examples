#version 460

layout(set = 0, binding = 0, r32ui) uniform uimage3D Voxels;

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(push_constant) uniform Cosntants {
    mat4 modelTransform;
    mat4 orthoMatrix;
};

layout(location = 0) out struct {
    vec3 position;
    vec3 normal;
    vec3 color;
    vec2 uv;
} vs_out;

void main(){
    ivec3 volumeDim = imageSize(Voxels);
    vec4 vertexPos  =  modelTransform * vec4(position.xyz, 1.0);
    vertexPos.xyz  *= volumeDim;
    vs_out.position   = vertexPos.xyz;
    vs_out.color = color.rgb;
    vs_out.normal = normal;
    vs_out.uv = uv;
    gl_Position = vertexPos;
}