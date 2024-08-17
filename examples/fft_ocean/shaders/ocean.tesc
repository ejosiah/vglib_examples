#version 460

layout (vertices = 4) out;

layout(location = 0) in vec2 vs_uv[gl_MaxPatchVertices];
layout(location = 1) flat in int patchId[gl_MaxPatchVertices];

layout(location = 0)  out vec2 tc_uv[4];
layout(location = 1) flat out int tc_patchId[4];

void main(){
    if(gl_InvocationID == 0){
        gl_TessLevelOuter[0] = 64;
        gl_TessLevelOuter[1] = 64;
        gl_TessLevelOuter[2] = 64;
        gl_TessLevelOuter[3] = 64;
        gl_TessLevelInner[0] = 64;
        gl_TessLevelInner[1] = 64;
    }
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    tc_uv[gl_InvocationID] = vs_uv[gl_InvocationID];
    tc_patchId[gl_InvocationID] = patchId[gl_InvocationID];
}