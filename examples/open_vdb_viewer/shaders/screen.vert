#version 460

layout(location = 0) out struct {
    vec2 uv;
} vs_out;

void main(){
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vs_out.uv = uv;
    gl_Position = vec4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
}