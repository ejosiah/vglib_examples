#version 460

layout (location = 0) out vec2 uv;
layout (location = 1) flat out uint out_texture_id;

void main() {

    uv.xy = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv.xy * 2.0f - 1.0f, 0.0f, 1.0f);

    out_texture_id = gl_InstanceIndex;
}
